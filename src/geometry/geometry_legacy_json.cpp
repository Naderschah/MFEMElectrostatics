#include "geometry_internal.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace geometry::detail {
namespace {

static bool hasKey(const json &j, const std::string &key) {
  return j.is_object() && j.find(key) != j.end() && !j.at(key).is_null();
}

template <typename T>
T getOr(const json &j, const std::string &key, const T &def) {
  if(!j.is_object()) return def;
  auto it = j.find(key);
  if(it == j.end() || it->is_null()) return def;
  try { return it->get<T>(); } catch(...) { return def; }
}

static Vec2 getVec2Or(const json &j, const std::string &key, Vec2 def = {}) {
  if(!j.is_object()) return def;
  auto it = j.find(key);
  if(it == j.end() || !it->is_array() || it->size() < 2) return def;
  try { return Vec2{(*it)[0].get<double>(), (*it)[1].get<double>()}; } catch(...) { return def; }
}

static CurveSegment::Kind parseKind(const std::string &kind) {
  if(kind == "line") return CurveSegment::Kind::Line;
  if(kind == "arc") return CurveSegment::Kind::Arc;
  if(kind == "ellipse" || kind == "ellipse_arc") return CurveSegment::Kind::EllipseArc;
  if(kind == "spline" || kind == "bspline") return CurveSegment::Kind::BSpline;
  if(kind == "hyperbola") return CurveSegment::Kind::HyperbolaNurbs;
  return CurveSegment::Kind::Line;
}

static CurveSegment parseExplicitSegment(const json &j) {
  CurveSegment seg;
  const std::string kind = getOr<std::string>(j, "kind", "line");
  seg.kind = parseKind(kind);
  seg.start = getVec2Or(j, "start");
  seg.end = getVec2Or(j, "end");
  if(hasKey(j, "center")) seg.center = getVec2Or(j, "center");
  seg.orientation = getOr<bool>(j, "orientation", true);
  seg.a = getOr<double>(j, "a", 0.0);
  seg.b = getOr<double>(j, "b", 0.0);
  seg.phiDeg = getOr<double>(j, "phiDeg", getOr<double>(j, "phi_deg", 0.0));
  seg.inversed = getOr<bool>(j, "inversed", false);
  seg.degree = getOr<int>(j, "degree", 3);
  seg.periodic = getOr<bool>(j, "periodic", false);
  if(hasKey(j, "poles") && j["poles"].is_array()) {
    for(const auto &p : j["poles"]) {
      if(p.is_array() && p.size() >= 2) {
        try { seg.poles.push_back(Vec2{p[0].get<double>(), p[1].get<double>()}); } catch(...) {}
      }
    }
  }
  if(hasKey(j, "weights")) try { seg.weights = j["weights"].get<std::vector<double>>(); } catch(...) {}
  if(hasKey(j, "knots")) try { seg.knots = j["knots"].get<std::vector<double>>(); } catch(...) {}
  if(hasKey(j, "multiplicities")) try { seg.multiplicities = j["multiplicities"].get<std::vector<int>>(); } catch(...) {}
  if(seg.multiplicities.empty() && hasKey(j, "mults")) try { seg.multiplicities = j["mults"].get<std::vector<int>>(); } catch(...) {}
  return seg;
}

static std::vector<CurveSegment> parseLegacyPtsContour(const json &pts) {
  std::vector<CurveSegment> out;
  if(!pts.is_array() || pts.empty()) return out;
  const std::size_t n = pts.size();
  auto pointFromRow = [](const json &row) -> Vec2 {
    if(!row.is_array() || row.size() < 3) return {};
    try { return Vec2{row[1].get<double>(), row[2].get<double>()}; } catch(...) { return {}; }
  };
  for(std::size_t i = 0; i < n; ++i) {
    const json &row = pts[i];
    const json &next = pts[(i + 1) % n];
    if(!row.is_array() || row.empty()) continue;
    std::string kind = "line";
    try { kind = row[0].get<std::string>(); } catch(...) {}
    CurveSegment seg;
    seg.kind = parseKind(kind);
    seg.start = pointFromRow(row);
    seg.end = pointFromRow(next);
    if(kind == "arc") {
      if(row.size() >= 5) try { seg.center = Vec2{row[3].get<double>(), row[4].get<double>()}; } catch(...) {}
      if(row.size() >= 6) try { seg.orientation = row[5].get<bool>(); } catch(...) {}
    } else if(kind == "ellipse") {
      if(row.size() >= 9) {
        try {
          seg.center = Vec2{row[3].get<double>(), row[4].get<double>()};
          seg.a = row[5].get<double>();
          seg.b = row[6].get<double>();
          seg.phiDeg = row[7].get<double>();
          seg.inversed = row[8].get<bool>();
        } catch(...) {}
      }
    } else if(kind == "spline") {
      if(row.size() >= 4 && row[3].is_object()) {
        const json &payload = row[3];
        seg.degree = getOr<int>(payload, "degree", 3);
        seg.periodic = getOr<bool>(payload, "periodic", false);
        if(hasKey(payload, "poles") && payload["poles"].is_array()) {
          for(const auto &p : payload["poles"]) {
            if(p.is_array() && p.size() >= 2) {
              try { seg.poles.push_back(Vec2{p[0].get<double>(), p[1].get<double>()}); } catch(...) {}
            }
          }
        }
        if(hasKey(payload, "weights")) try { seg.weights = payload["weights"].get<std::vector<double>>(); } catch(...) {}
        if(hasKey(payload, "knots")) try { seg.knots = payload["knots"].get<std::vector<double>>(); } catch(...) {}
        if(hasKey(payload, "mults")) try { seg.multiplicities = payload["mults"].get<std::vector<int>>(); } catch(...) {}
        if(seg.multiplicities.empty() && hasKey(payload, "multiplicities")) try { seg.multiplicities = payload["multiplicities"].get<std::vector<int>>(); } catch(...) {}
      }
    }
    out.push_back(std::move(seg));
  }
  return out;
}

static Component parseComponent(const json &j) {
  Component c;
  c.name = getOr<std::string>(j, "name", getOr<std::string>(j, "Name", ""));
  c.material = getOr<std::string>(j, "material", getOr<std::string>(j, "Material", c.name));
  c.hullOnly = getOr<bool>(j, "hullOnly", getOr<bool>(j, "hull", false));
  c.markBoundary = getOr<bool>(j, "markBoundary", false);
  c.isAxisBoundary = getOr<bool>(j, "isAxisBoundary", false);
  c.isElectrodeBoundary = getOr<bool>(j, "isElectrodeBoundary", false);
  c.groupBoundary = getOr<bool>(j, "groupBoundary", false);
  c.boundaryName = getOr<std::string>(j, "boundaryName", getOr<std::string>(j, "bcName", ""));
  c.splitBoundaryByIndex = getOr<bool>(
    j, "splitBoundaryByIndex",
    getOr<bool>(j, "splitBoundaryInstances", getOr<bool>(j, "splitIndexedBC", false)));
  c.applyShrinkage = getOr<bool>(j, "applyShrinkage", false);
  c.shrinkBelowY = getOr<double>(j, "shrinkBelowY", 0.0);
  const bool shrinkBelowYExplicit = getOr<bool>(j, "shrinkBelowYExplicit", false);
  c.hasShrinkBelowY = hasKey(j, "shrinkBelowY")
                      && (shrinkBelowYExplicit || std::abs(c.shrinkBelowY) > 1e-15);
  c.shrinkageFactor = getOr<double>(j, "shrinkageFactor", 1.0);
  if(hasKey(j, "repeat") && j["repeat"].is_object()) {
    RepeatSpec r;
    r.number = getOr<int>(j["repeat"], "number", 1);
    r.dx = getOr<double>(j["repeat"], "dx", 0.0);
    r.dy = getOr<double>(j["repeat"], "dy", 0.0);
    c.repeat = r;
  } else {
    int number = getOr<int>(j, "Number", 1);
    double dx = getOr<double>(j, "HorizontalPitch", 0.0);
    double dy = getOr<double>(j, "VerticalPitch", 0.0);
    if(number > 1 || std::abs(dx) > 0.0 || std::abs(dy) > 0.0) c.repeat = RepeatSpec{number, dx, dy};
  }
  if(hasKey(j, "outer") && j["outer"].is_array()) {
    for(const auto &seg : j["outer"]) c.outer.push_back(parseExplicitSegment(seg));
  } else if(hasKey(j, "pts")) {
    c.outer = parseLegacyPtsContour(j["pts"]);
  }
  if(hasKey(j, "holes") && j["holes"].is_array()) {
    for(const auto &hole : j["holes"]) {
      std::vector<CurveSegment> h;
      if(hole.is_array()) {
        bool explicitSegs = false;
        for(const auto &seg : hole) if(seg.is_object()) { explicitSegs = true; break; }
        if(explicitSegs) {
          for(const auto &seg : hole) h.push_back(parseExplicitSegment(seg));
        } else {
          h = parseLegacyPtsContour(hole);
        }
      }
      if(!h.empty()) c.holes.push_back(std::move(h));
    }
  }
  if(hasKey(j, "children") && j["children"].is_array()) {
    for(const auto &child : j["children"]) c.children.push_back(parseComponent(child));
  }
  if(hasKey(j, "sub_sketches") && j["sub_sketches"].is_object()) {
    for(auto it = j["sub_sketches"].begin(); it != j["sub_sketches"].end(); ++it) {
      json child = it.value();
      if(!hasKey(child, "name")) child["name"] = it.key();
      c.children.push_back(parseComponent(child));
    }
  }
  return c;
}

static std::optional<double> optionalDouble(const json &j, const std::string &key) {
  if(!hasKey(j, key)) return std::nullopt;
  try { return j.at(key).get<double>(); } catch(...) { return std::nullopt; }
}

static LoadedGeometry loadComponentsFromJson(const std::filesystem::path &path) {
  std::ifstream in(path);
  if(!in) throw std::runtime_error("failed to open JSON file: " + path.string());
  json j;
  in >> j;

  LoadedGeometry out;
  out.sourceFormat = GeometrySourceFormat::LegacyJson;

  if(j.is_object()) {
    out.liquidLevel = optionalDouble(j, "liquidLevel");
    if(!out.liquidLevel) out.liquidLevel = optionalDouble(j, "liquid_level");
    if(!out.liquidLevel && hasKey(j, "metadata") && j["metadata"].is_object()) {
      out.liquidLevel = optionalDouble(j["metadata"], "liquidLevel");
      if(!out.liquidLevel) out.liquidLevel = optionalDouble(j["metadata"], "liquid_level");
    }
  }

  if(hasKey(j, "components") && j["components"].is_array()) {
    for(const auto &c : j["components"]) out.components.push_back(parseComponent(c));
  } else if(j.is_array()) {
    for(const auto &c : j) out.components.push_back(parseComponent(c));
  } else if(j.is_object()) {
    for(auto it = j.begin(); it != j.end(); ++it) {
      if(!it.value().is_object()) continue;
      json child = it.value();
      if(!hasKey(child, "name")) child["name"] = it.key();
      out.components.push_back(parseComponent(child));
    }
  }

  return out;
}

} // namespace

GeometrySourceFormat inferGeometrySourceFormat(const std::filesystem::path &path) {
  const std::string ext = path.extension().string();
  if(ext == ".json") return GeometrySourceFormat::LegacyJson;
  if(ext == ".dxf") return GeometrySourceFormat::Dxf;
  throw std::runtime_error("unsupported geometry source extension '" + ext + "' for " + path.string());
}

const char *geometrySourceFormatName(GeometrySourceFormat format) {
  switch(format) {
    case GeometrySourceFormat::LegacyJson: return "json";
    case GeometrySourceFormat::Dxf: return "dxf";
  }
  return "unknown";
}

LoadedGeometry loadGeometryFromLegacyJsonSource(const std::filesystem::path &path) {
  if(inferGeometrySourceFormat(path) != GeometrySourceFormat::LegacyJson) {
    throw std::runtime_error("expected legacy JSON geometry source, got " + path.string());
  }
  return loadComponentsFromJson(path);
}

} // namespace geometry::detail
