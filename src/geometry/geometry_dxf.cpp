#include "geometry_internal.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace geometry::detail {
namespace {

constexpr double kPi = 3.141592653589793238462643383279502884;

struct LayerMetadata {
  std::string rawName;
  std::string baseName;
  std::optional<std::string> material;
  bool perComponentVoltage = false;
};

struct DxfCodePair {
  int code = 0;
  std::string value;
};

struct DxfEntity {
  std::string type;
  std::vector<DxfCodePair> fields;
};

struct LoopRecord {
  std::vector<CurveSegment> segments;
  std::vector<Vec2> polygon;
  double signedArea = 0.0;
  double areaAbs = 0.0;
  Vec2 centroid{};
  int parent = -1;
  std::vector<int> children;
};

static std::string trimCopy(const std::string &s) {
  std::size_t b = 0;
  while(b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  std::size_t e = s.size();
  while(e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

static std::string lowerCopy(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

static bool samePoint(const Vec2 &a, const Vec2 &b, const double tol) {
  return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol;
}

static double distanceSquared(const Vec2 &a, const Vec2 &b) {
  const double dx = a.x - b.x;
  const double dy = a.y - b.y;
  return dx * dx + dy * dy;
}

static double normalizeDegrees(double deg) {
  while(deg < 0.0) deg += 360.0;
  while(deg >= 360.0) deg -= 360.0;
  return deg;
}

static double normalizeRadians(double rad) {
  while(rad < 0.0) rad += 2.0 * kPi;
  while(rad >= 2.0 * kPi) rad -= 2.0 * kPi;
  return rad;
}

static std::optional<std::string> fieldValue(const DxfEntity &entity, const int code) {
  for(auto it = entity.fields.rbegin(); it != entity.fields.rend(); ++it) {
    if(it->code == code) return it->value;
  }
  return std::nullopt;
}

static double requireDouble(const DxfEntity &entity, const int code, const std::string &label) {
  const auto raw = fieldValue(entity, code);
  if(!raw) {
    throw std::runtime_error(entity.type + " entity missing DXF group " +
                             std::to_string(code) + " (" + label + ")");
  }
  try {
    return std::stod(*raw);
  } catch(...) {
    throw std::runtime_error(entity.type + " entity has invalid numeric group " +
                             std::to_string(code) + " value '" + *raw + "'");
  }
}

static LayerMetadata parseLayerMetadata(const std::string &layerName) {
  LayerMetadata out;
  out.rawName = layerName;

  std::size_t pos = layerName.find("__");
  out.baseName = pos == std::string::npos ? layerName : layerName.substr(0, pos);
  if(out.baseName.empty()) {
    throw std::runtime_error("DXF layer '" + layerName + "' has an empty base name.");
  }

  std::set<std::string> seenKeys;
  while(pos != std::string::npos) {
    const std::size_t next = layerName.find("__", pos + 2);
    const std::string token = layerName.substr(
      pos + 2, next == std::string::npos ? std::string::npos : next - (pos + 2));
    std::string key;
    std::string value;
    if(token.rfind("material_", 0) == 0) {
      key = "material";
      value = token.substr(std::string("material_").size());
      if(value.empty()) {
        throw std::runtime_error("DXF layer '" + layerName +
                                 "' has invalid metadata token '" + token + "'.");
      }
    } else if(token == "voltage_per_component") {
      key = "voltage";
      value = "per_component";
    } else {
      throw std::runtime_error("DXF layer '" + layerName +
                               "' has invalid metadata token '" + token + "'.");
    }
    if(!seenKeys.insert(key).second) {
      throw std::runtime_error("DXF layer '" + layerName +
                               "' repeats metadata key '" + key + "'.");
    }
    if(key == "material") {
      out.material = value;
    } else if(key == "voltage") {
      if(value != "per_component") {
        throw std::runtime_error("DXF layer '" + layerName +
                                 "' has unsupported voltage mode '" + value +
                                 "'. Only 'per_component' is supported.");
      }
      out.perComponentVoltage = true;
    } else {
      throw std::runtime_error("DXF layer '" + layerName +
                               "' has unsupported metadata key '" + key + "'.");
    }
    pos = next;
  }

  if(out.material && out.perComponentVoltage) {
    throw std::runtime_error("DXF layer '" + layerName +
                             "' mixes volumetric material metadata with voltage splitting.");
  }

  return out;
}

static CurveSegment entityToSegment(const DxfEntity &entity) {
  const auto layer = fieldValue(entity, 8).value_or("0");
  auto extrusionX = fieldValue(entity, 210);
  auto extrusionY = fieldValue(entity, 220);
  auto extrusionZ = fieldValue(entity, 230);
  const double ex = extrusionX ? std::stod(*extrusionX) : 0.0;
  const double ey = extrusionY ? std::stod(*extrusionY) : 0.0;
  const double ez = extrusionZ ? std::stod(*extrusionZ) : 1.0;
  if(std::abs(ex) > 1e-12 || std::abs(ey) > 1e-12 || std::abs(ez - 1.0) > 1e-12) {
    throw std::runtime_error("DXF entity on layer '" + layer +
                             "' uses unsupported extrusion; expected +Z planar entities.");
  }

  if(entity.type == "LINE") {
    CurveSegment seg;
    seg.kind = CurveSegment::Kind::Line;
    seg.start = Vec2{
      requireDouble(entity, 10, "x0"),
      requireDouble(entity, 20, "y0"),
    };
    seg.end = Vec2{
      requireDouble(entity, 11, "x1"),
      requireDouble(entity, 21, "y1"),
    };
    return seg;
  }

  if(entity.type == "ARC") {
    const double cx = requireDouble(entity, 10, "center x");
    const double cy = requireDouble(entity, 20, "center y");
    const double radius = requireDouble(entity, 40, "radius");
    const double startDeg = normalizeDegrees(requireDouble(entity, 50, "start angle"));
    const double endDeg = normalizeDegrees(requireDouble(entity, 51, "end angle"));
    const double startRad = startDeg * kPi / 180.0;
    const double endRad = endDeg * kPi / 180.0;

    CurveSegment seg;
    seg.kind = CurveSegment::Kind::Arc;
    seg.center = Vec2{cx, cy};
    seg.start = Vec2{cx + radius * std::cos(startRad), cy + radius * std::sin(startRad)};
    seg.end = Vec2{cx + radius * std::cos(endRad), cy + radius * std::sin(endRad)};
    return seg;
  }

  if(entity.type == "CIRCLE") {
    throw std::runtime_error("internal error: CIRCLE must be expanded before segment conversion");
  }

  throw std::runtime_error("unsupported DXF entity type '" + entity.type + "'");
}

static std::vector<CurveSegment> circleToSegments(const DxfEntity &entity) {
  const double cx = requireDouble(entity, 10, "center x");
  const double cy = requireDouble(entity, 20, "center y");
  const double radius = requireDouble(entity, 40, "radius");

  std::vector<CurveSegment> out;
  out.reserve(4);
  for(int i = 0; i < 4; ++i) {
    const double a0 = (0.5 * kPi) * static_cast<double>(i);
    const double a1 = (0.5 * kPi) * static_cast<double>(i + 1);
    CurveSegment seg;
    seg.kind = CurveSegment::Kind::Arc;
    seg.center = Vec2{cx, cy};
    seg.start = Vec2{cx + radius * std::cos(a0), cy + radius * std::sin(a0)};
    seg.end = Vec2{cx + radius * std::cos(a1), cy + radius * std::sin(a1)};
    out.push_back(seg);
  }
  return out;
}

static std::vector<DxfEntity> parseEntities(const std::filesystem::path &path) {
  std::ifstream in(path);
  if(!in) {
    throw std::runtime_error("failed to open DXF file: " + path.string());
  }

  std::vector<DxfCodePair> pairs;
  std::string codeLine;
  std::string valueLine;
  while(std::getline(in, codeLine)) {
    if(!std::getline(in, valueLine)) {
      throw std::runtime_error("DXF file has an odd number of lines: " + path.string());
    }
    const std::string codeTrimmed = trimCopy(codeLine);
    if(codeTrimmed.empty()) {
      throw std::runtime_error("DXF file contains an empty group code line: " + path.string());
    }
    int code = 0;
    try {
      code = std::stoi(codeTrimmed);
    } catch(...) {
      throw std::runtime_error("DXF file contains an invalid group code '" +
                               codeTrimmed + "': " + path.string());
    }
    pairs.push_back(DxfCodePair{code, trimCopy(valueLine)});
  }

  bool inEntities = false;
  std::vector<DxfEntity> entities;
  for(std::size_t i = 0; i < pairs.size();) {
    const DxfCodePair &pair = pairs[i];
    if(pair.code == 0 && pair.value == "SECTION") {
      if(i + 1 >= pairs.size() || pairs[i + 1].code != 2) {
        throw std::runtime_error("DXF SECTION without section name in " + path.string());
      }
      inEntities = (pairs[i + 1].value == "ENTITIES");
      i += 2;
      continue;
    }
    if(pair.code == 0 && pair.value == "ENDSEC") {
      inEntities = false;
      ++i;
      continue;
    }
    if(!inEntities) {
      ++i;
      continue;
    }
    if(pair.code != 0) {
      throw std::runtime_error("DXF ENTITIES section is malformed near group code " +
                               std::to_string(pair.code) + " in " + path.string());
    }

    DxfEntity entity;
    entity.type = pair.value;
    ++i;
    while(i < pairs.size() && pairs[i].code != 0) {
      entity.fields.push_back(pairs[i]);
      ++i;
    }

    if(entity.type == "LINE" || entity.type == "ARC" || entity.type == "CIRCLE") {
      entities.push_back(std::move(entity));
    } else if(entity.type == "ENDSEC") {
      inEntities = false;
    } else {
      throw std::runtime_error("unsupported DXF entity type '" + entity.type +
                               "' in " + path.string());
    }
  }

  if(entities.empty()) {
    throw std::runtime_error("DXF file contains no supported ENTITIES: " + path.string());
  }

  return entities;
}

static double estimateTolerance(const std::vector<CurveSegment> &segments) {
  double scale = 1.0;
  for(const auto &seg : segments) {
    scale = std::max(scale, std::abs(seg.start.x));
    scale = std::max(scale, std::abs(seg.start.y));
    scale = std::max(scale, std::abs(seg.end.x));
    scale = std::max(scale, std::abs(seg.end.y));
    if(seg.center) {
      scale = std::max(scale, std::abs(seg.center->x));
      scale = std::max(scale, std::abs(seg.center->y));
    }
  }
  return std::max(1e-7, 1e-6 * scale);
}

static std::vector<CurveSegment> orderSegmentsForClosedLoop(std::vector<CurveSegment> segments,
                                                            const std::string &label,
                                                            const double tol) {
  if(segments.empty()) {
    throw std::runtime_error("DXF loop '" + label + "' is empty.");
  }

  auto reverseSegment = [](CurveSegment seg) {
    std::swap(seg.start, seg.end);
    if(seg.kind == CurveSegment::Kind::Arc || seg.kind == CurveSegment::Kind::EllipseArc) {
      seg.orientation = !seg.orientation;
    }
    seg.inversed = !seg.inversed;
    return seg;
  };

  std::vector<CurveSegment> ordered;
  std::vector<bool> used(segments.size(), false);
  ordered.push_back(segments.front());
  used[0] = true;
  Vec2 currentEnd = ordered.back().end;

  while(ordered.size() < segments.size()) {
    std::size_t nextIdx = segments.size();
    bool reverseNext = false;
    int matchCount = 0;
    for(std::size_t i = 0; i < segments.size(); ++i) {
      if(used[i]) continue;
      if(samePoint(segments[i].start, currentEnd, tol)) {
        nextIdx = i;
        reverseNext = false;
        ++matchCount;
      }
      if(samePoint(segments[i].end, currentEnd, tol)) {
        nextIdx = i;
        reverseNext = true;
        ++matchCount;
      }
    }
    if(matchCount > 1) {
      std::size_t nonClosingMatches = 0;
      std::size_t candidateIdx = segments.size();
      bool candidateReverse = false;
      for(std::size_t i = 0; i < segments.size(); ++i) {
        if(used[i]) continue;
        if(samePoint(segments[i].start, currentEnd, tol) &&
           !samePoint(segments[i].end, ordered.front().start, tol)) {
          candidateIdx = i;
          candidateReverse = false;
          ++nonClosingMatches;
        }
        if(samePoint(segments[i].end, currentEnd, tol) &&
           !samePoint(segments[i].start, ordered.front().start, tol)) {
          candidateIdx = i;
          candidateReverse = true;
          ++nonClosingMatches;
        }
      }
      if(nonClosingMatches == 1) {
        nextIdx = candidateIdx;
        reverseNext = candidateReverse;
        matchCount = 1;
      } else if(nonClosingMatches > 1) {
        throw std::runtime_error("DXF layer '" + label +
                                 "' has ambiguous loop ordering at a shared endpoint.");
      }
    }
    if(nextIdx == segments.size()) {
      break;
    }

    CurveSegment next = reverseNext ? reverseSegment(segments[nextIdx]) : segments[nextIdx];
    if(!samePoint(next.start, currentEnd, tol)) {
      throw std::runtime_error("internal DXF loop ordering error for '" + label + "'.");
    }
    next.start = currentEnd;
    ordered.push_back(std::move(next));
    used[nextIdx] = true;
    currentEnd = ordered.back().end;
  }

  if(ordered.size() != segments.size()) {
    throw std::runtime_error("DXF layer '" + label +
                             "' did not form a single directed closed loop.");
  }
  if(!samePoint(ordered.back().end, ordered.front().start, tol)) {
    throw std::runtime_error("DXF layer '" + label + "' produced an open loop.");
  }
  ordered.back().end = ordered.front().start;
  for(std::size_t i = 1; i < ordered.size(); ++i) {
    ordered[i].start = ordered[i - 1].end;
  }
  return ordered;
}

struct EndpointKey {
  long long x = 0;
  long long y = 0;

  bool operator==(const EndpointKey &other) const {
    return x == other.x && y == other.y;
  }
};

struct EndpointKeyHasher {
  std::size_t operator()(const EndpointKey &k) const {
    return std::hash<long long>{}(k.x) ^ (std::hash<long long>{}(k.y) << 1);
  }
};

static EndpointKey quantizePoint(const Vec2 &p, const double tol) {
  return EndpointKey{
    static_cast<long long>(std::llround(p.x / tol)),
    static_cast<long long>(std::llround(p.y / tol)),
  };
}

static std::vector<std::vector<CurveSegment>>
splitIntoClosedLoops(const std::vector<CurveSegment> &segments,
                     const std::string &label) {
  if(segments.empty()) return {};

  const double tol = estimateTolerance(segments);
  std::unordered_map<EndpointKey, std::vector<int>, EndpointKeyHasher> incident;
  incident.reserve(segments.size() * 2);
  for(std::size_t i = 0; i < segments.size(); ++i) {
    incident[quantizePoint(segments[i].start, tol)].push_back(static_cast<int>(i));
    incident[quantizePoint(segments[i].end, tol)].push_back(static_cast<int>(i));
  }

  for(const auto &[key, ids] : incident) {
    (void)key;
    if(ids.size() != 2) {
      throw std::runtime_error("DXF layer '" + label +
                               "' contains an open or branching endpoint graph.");
    }
  }

  std::vector<std::vector<CurveSegment>> loops;
  std::vector<bool> used(segments.size(), false);
  for(std::size_t i = 0; i < segments.size(); ++i) {
    if(used[i]) continue;
    std::vector<int> stack{static_cast<int>(i)};
    used[i] = true;
    std::vector<CurveSegment> component;
    while(!stack.empty()) {
      const int idx = stack.back();
      stack.pop_back();
      component.push_back(segments[idx]);
      for(const EndpointKey &key : {quantizePoint(segments[idx].start, tol), quantizePoint(segments[idx].end, tol)}) {
        const auto it = incident.find(key);
        if(it == incident.end()) continue;
        for(const int other : it->second) {
          if(!used[other]) {
            used[other] = true;
            stack.push_back(other);
          }
        }
      }
    }
    loops.push_back(orderSegmentsForClosedLoop(std::move(component), label, tol));
  }

  return loops;
}

static std::vector<Vec2> sampleLoopPolygon(const std::vector<CurveSegment> &segments) {
  std::vector<Vec2> pts;
  for(const auto &seg : segments) {
    if(pts.empty()) {
      pts.push_back(seg.start);
    }
    if(seg.kind == CurveSegment::Kind::Line) {
      pts.push_back(seg.end);
      continue;
    }
    if(seg.kind == CurveSegment::Kind::Arc && seg.center) {
      const double startAngle = normalizeRadians(std::atan2(seg.start.y - seg.center->y,
                                                            seg.start.x - seg.center->x));
      const double endAngle = normalizeRadians(std::atan2(seg.end.y - seg.center->y,
                                                          seg.end.x - seg.center->x));
      double delta = endAngle - startAngle;
      if(seg.orientation) {
        if(delta <= 0.0) delta += 2.0 * kPi;
      } else {
        if(delta >= 0.0) delta -= 2.0 * kPi;
      }
      const int n = std::max(
        4,
        static_cast<int>(std::ceil(std::abs(delta) / (kPi / 18.0))));
      const double radius = std::hypot(seg.start.x - seg.center->x, seg.start.y - seg.center->y);
      for(int i = 1; i < n; ++i) {
        const double angle = startAngle + delta * (static_cast<double>(i) / static_cast<double>(n));
        pts.push_back(Vec2{
          seg.center->x + radius * std::cos(angle),
          seg.center->y + radius * std::sin(angle),
        });
      }
      pts.push_back(seg.end);
      continue;
    }
    throw std::runtime_error("DXF sampling encountered unsupported curve kind.");
  }
  if(!pts.empty() && samePoint(pts.front(), pts.back(), 1e-9)) {
    pts.pop_back();
  }
  return pts;
}

static double signedArea(const std::vector<Vec2> &poly) {
  if(poly.size() < 3) return 0.0;
  double area = 0.0;
  for(std::size_t i = 0; i < poly.size(); ++i) {
    const Vec2 &a = poly[i];
    const Vec2 &b = poly[(i + 1) % poly.size()];
    area += a.x * b.y - b.x * a.y;
  }
  return 0.5 * area;
}

static Vec2 polygonCentroid(const std::vector<Vec2> &poly) {
  Vec2 c{};
  double area6 = 0.0;
  for(std::size_t i = 0; i < poly.size(); ++i) {
    const Vec2 &a = poly[i];
    const Vec2 &b = poly[(i + 1) % poly.size()];
    const double cross = a.x * b.y - b.x * a.y;
    area6 += cross;
    c.x += (a.x + b.x) * cross;
    c.y += (a.y + b.y) * cross;
  }
  if(std::abs(area6) < 1e-18) {
    for(const auto &p : poly) {
      c.x += p.x;
      c.y += p.y;
    }
    if(!poly.empty()) {
      c.x /= static_cast<double>(poly.size());
      c.y /= static_cast<double>(poly.size());
    }
    return c;
  }
  c.x /= (3.0 * area6);
  c.y /= (3.0 * area6);
  return c;
}

static bool pointInPolygon(const Vec2 &p, const std::vector<Vec2> &poly) {
  bool inside = false;
  for(std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
    const Vec2 &pi = poly[i];
    const Vec2 &pj = poly[j];
    const bool intersect =
      ((pi.y > p.y) != (pj.y > p.y)) &&
      (p.x < (pj.x - pi.x) * (p.y - pi.y) / ((pj.y - pi.y) == 0.0 ? 1e-30 : (pj.y - pi.y)) + pi.x);
    if(intersect) inside = !inside;
  }
  return inside;
}

static std::vector<LoopRecord> classifyLoops(const std::vector<std::vector<CurveSegment>> &loops) {
  std::vector<LoopRecord> records;
  records.reserve(loops.size());
  for(const auto &segments : loops) {
    LoopRecord rec;
    rec.segments = segments;
    rec.polygon = sampleLoopPolygon(segments);
    if(rec.polygon.size() < 3) {
      throw std::runtime_error("DXF loop has fewer than three distinct sample points.");
    }
    rec.signedArea = signedArea(rec.polygon);
    rec.areaAbs = std::abs(rec.signedArea);
    rec.centroid = polygonCentroid(rec.polygon);
    records.push_back(std::move(rec));
  }

  for(std::size_t i = 0; i < records.size(); ++i) {
    int bestParent = -1;
    double bestArea = std::numeric_limits<double>::infinity();
    for(std::size_t j = 0; j < records.size(); ++j) {
      if(i == j) continue;
      if(records[j].areaAbs <= records[i].areaAbs) continue;
      if(!pointInPolygon(records[i].polygon.front(), records[j].polygon)) continue;
      if(records[j].areaAbs < bestArea) {
        bestArea = records[j].areaAbs;
        bestParent = static_cast<int>(j);
      }
    }
    records[i].parent = bestParent;
  }

  for(std::size_t i = 0; i < records.size(); ++i) {
    const int parent = records[i].parent;
    if(parent >= 0) {
      records[parent].children.push_back(static_cast<int>(i));
    }
  }

  return records;
}

static Component buildComponentFromEvenLoop(const std::vector<LoopRecord> &loops, const int idx) {
  Component c;
  c.outer = loops[idx].segments;
  for(const int child : loops[idx].children) {
    c.holes.push_back(loops[child].segments);
    for(const int grandchild : loops[child].children) {
      c.children.push_back(buildComponentFromEvenLoop(loops, grandchild));
    }
  }
  return c;
}

static std::vector<Component> buildTopLevelComponents(std::vector<LoopRecord> loops) {
  std::vector<Component> topLevel;
  std::vector<int> roots;
  for(std::size_t i = 0; i < loops.size(); ++i) {
    if(loops[i].parent < 0) roots.push_back(static_cast<int>(i));
  }

  std::sort(roots.begin(), roots.end(),
            [&](const int a, const int b) {
              if(std::abs(loops[a].centroid.y - loops[b].centroid.y) > 1e-12) {
                return loops[a].centroid.y > loops[b].centroid.y;
              }
              if(std::abs(loops[a].centroid.x - loops[b].centroid.x) > 1e-12) {
                return loops[a].centroid.x > loops[b].centroid.x;
              }
              return loops[a].areaAbs > loops[b].areaAbs;
            });

  for(const int idx : roots) {
    topLevel.push_back(buildComponentFromEvenLoop(loops, idx));
  }

  return topLevel;
}

static std::vector<Component> loadComponentsFromDxf(const std::filesystem::path &path) {
  const std::vector<DxfEntity> entities = parseEntities(path);
  std::map<std::string, std::vector<CurveSegment>> byLayer;

  for(const DxfEntity &entity : entities) {
    const std::string layer = fieldValue(entity, 8).value_or("");
    if(layer.empty()) {
      throw std::runtime_error("DXF entity '" + entity.type + "' is missing layer metadata.");
    }
    if(entity.type == "CIRCLE") {
      auto expanded = circleToSegments(entity);
      auto &dest = byLayer[layer];
      dest.insert(dest.end(), expanded.begin(), expanded.end());
    } else {
      byLayer[layer].push_back(entityToSegment(entity));
    }
  }

  std::vector<Component> roots;
  roots.reserve(byLayer.size());
  for(auto &[layerName, segments] : byLayer) {
    LayerMetadata meta = parseLayerMetadata(layerName);
    const std::vector<std::vector<CurveSegment>> loops = splitIntoClosedLoops(segments, layerName);
    const std::vector<LoopRecord> classified = classifyLoops(loops);
    std::vector<Component> children = buildTopLevelComponents(classified);
    if(children.empty()) {
      throw std::runtime_error("DXF layer '" + layerName + "' produced no closed components.");
    }

    Component root;
    root.name = meta.baseName;
    root.hullOnly = true;
    root.children = std::move(children);
    if(meta.material) {
      root.material = *meta.material;
    } else {
      root.material = "ELECTRODE";
      root.boundaryName = meta.baseName;
      root.markBoundary = true;
      root.groupBoundary = true;
      root.isElectrodeBoundary = true;
      root.splitBoundaryByIndex = meta.perComponentVoltage;
    }
    roots.push_back(std::move(root));
  }

  return roots;
}

} // namespace

LoadedGeometry loadGeometryFromDxfSource(const std::filesystem::path &path) {
  if(inferGeometrySourceFormat(path) != GeometrySourceFormat::Dxf) {
    throw std::runtime_error("expected DXF geometry source, got " + path.string());
  }

  LoadedGeometry out;
  out.sourceFormat = GeometrySourceFormat::Dxf;
  out.components = loadComponentsFromDxf(path);
  return out;
}

} // namespace geometry::detail
