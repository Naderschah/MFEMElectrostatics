#include "geometry_api.h"
#include "geometry_internal.h"

#include <gmsh.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using geometry::detail::Component;
using geometry::detail::CurveSegment;
using geometry::detail::GeometrySourceFormat;
using geometry::detail::LoadedGeometry;
using geometry::detail::RepeatSpec;
using geometry::detail::Vec2;
using geometry::detail::geometrySourceFormatName;
using geometry::detail::inferGeometrySourceFormat;
using geometry::detail::loadGeometryFromDxfSource;
using geometry::detail::loadGeometryFromLegacyJsonSource;

bool gDebugLogEnabled = false;

template <typename... Args>
void debugLog(Args &&...args) {
  if(!gDebugLogEnabled) return;
  (std::cout << ... << std::forward<Args>(args));
}

constexpr const char *kFallbackBoundaryName = "BC";
constexpr const char *kAxisBoundaryName = "BC_Axis";
constexpr const char *kWallBoundaryName = "BC_WallCharge";
// Keep geometry build behavior consistent with mesh_gmsh.
constexpr double kCurveEndpointSnapTol = 1e-12;

struct PhysicalGroupRecord {
  int dim = -1;
  int tag = -1;
  std::string name;
  std::vector<int> entityTags;
};

struct RootClassification {
  std::string materialGroup;
  std::string bcName;
  bool exportFaces = true;
  bool splitIndexedBC = false;
};

struct BuildResult {
  std::vector<int> finalFaces;
  std::map<int, std::string> faceOwner;
  std::map<std::string, std::vector<int>> ownerFaces;
  std::vector<int> boundaryCurves;
  std::map<std::string, std::vector<int>> bcCurvesByName;
  std::map<std::string, PhysicalGroupRecord> physicalGroups;
};

struct FieldCageNodeConfig {
  std::string name;
  std::string boundary;
  bool fixed = false;
};

struct FieldCageEdgeConfig {
  std::string n1;
  std::string n2;
  std::string resistor;
};

struct FieldCageNetworkConfig {
  bool enabled = false;
  std::vector<std::string> rings;
  std::vector<std::string> guards;
  std::string cathodeBoundary;
  std::vector<FieldCageNodeConfig> nodes;
  std::map<std::string, double> resistors;
  std::vector<FieldCageEdgeConfig> edges;
  std::vector<std::string> warnings;
};

static std::string lowerCopy(const std::string &s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

static bool parseIndexedBoundaryName(const std::string &name,
                                     const std::string &base,
                                     int &index) {
  if(name.rfind(base, 0) != 0) return false;

  std::string suffix = name.substr(base.size());
  if(suffix.empty()) return false;
  if(suffix.front() == '_') suffix.erase(suffix.begin());
  if(suffix.empty()) return false;

  for(char c : suffix) {
    if(!std::isdigit(static_cast<unsigned char>(c))) return false;
  }

  try {
    index = std::stoi(suffix);
    return true;
  } catch(...) {
    return false;
  }
}

static std::vector<std::string> collectIndexedBoundaryNames(
  const std::map<std::string, PhysicalGroupRecord> &physicalGroups,
  const std::string &base) {

  std::vector<std::pair<int, std::string>> indexed;
  for(const auto &[name, rec] : physicalGroups) {
    if(rec.dim != 1) continue;
    int idx = -1;
    if(parseIndexedBoundaryName(name, base, idx)) {
      indexed.emplace_back(idx, name);
    }
  }

  std::sort(indexed.begin(), indexed.end(),
            [](const auto &a, const auto &b) {
              if(a.first != b.first) return a.first < b.first;
              return a.second < b.second;
            });

  std::vector<std::string> out;
  out.reserve(indexed.size());
  for(const auto &[idx, name] : indexed) {
    (void)idx;
    out.push_back(name);
  }
  return out;
}

static std::string findBoundaryContainingName(
  const std::map<std::string, PhysicalGroupRecord> &physicalGroups,
  const std::string &needle) {

  const std::string needleLower = lowerCopy(needle);
  std::string best;
  int bestTag = std::numeric_limits<int>::max();

  for(const auto &[name, rec] : physicalGroups) {
    if(rec.dim != 1) continue;
    const std::string lowered = lowerCopy(name);
    if(lowered.find(needleLower) == std::string::npos) continue;
    if(rec.tag < bestTag || (rec.tag == bestTag && name < best)) {
      bestTag = rec.tag;
      best = name;
    }
  }
  return best;
}

static FieldCageNetworkConfig buildFieldCageNetworkConfig(
  const std::map<std::string, PhysicalGroupRecord> &physicalGroups) {

  FieldCageNetworkConfig out;
  out.rings = collectIndexedBoundaryNames(physicalGroups, "BC_FieldShapingRings");
  out.guards = collectIndexedBoundaryNames(physicalGroups, "BC_FieldShapingGuard");
  out.cathodeBoundary = findBoundaryContainingName(physicalGroups, "cathode");
  out.resistors = {
    {"R1", 1.25e9},
    {"R2", 2.5e9},
    {"R3", 5.0e9},
    {"R_C", 7.0e9},
  };

  if(out.rings.empty()) {
    out.warnings.push_back(
      "No indexed FieldShaping ring boundaries found (expected BC_FieldShapingRings_<index>).");
    return out;
  }
  if(out.cathodeBoundary.empty()) {
    out.warnings.push_back("No cathode boundary name detected; field-cage network left disabled.");
    return out;
  }

  out.enabled = true;

  const std::string topRing = out.rings.front();
  for(const auto &name : out.rings) {
    out.nodes.push_back(FieldCageNodeConfig{name, name, name == topRing});
  }
  for(const auto &name : out.guards) {
    out.nodes.push_back(FieldCageNodeConfig{name, name, false});
  }
  out.nodes.push_back(FieldCageNodeConfig{"Cathode", out.cathodeBoundary, true});

  const int nFc = static_cast<int>(out.rings.size());
  const int nGuard = static_cast<int>(out.guards.size());

  auto addEdge = [&](const std::string &n1, const std::string &n2, const std::string &resistor) {
    if(n1.empty() || n2.empty() || resistor.empty()) return;
    out.edges.push_back(FieldCageEdgeConfig{n1, n2, resistor});
  };

  if(nFc < 8 || nGuard == 0) {
    for(int i = 0; i + 1 < nFc; ++i) {
      addEdge(out.rings[i], out.rings[i + 1], "R1");
    }
    addEdge(out.rings.back(), "Cathode", "R_C");
    return out;
  }

  for(int i = 0; i < 4 && (i + 1) < nFc; ++i) {
    addEdge(out.rings[i], out.rings[i + 1], "R1");
  }

  if(nFc > 5) addEdge(out.rings[4], out.rings[5], "R2");
  addEdge(out.rings[4], out.guards[0], "R2");

  const int mergeIndex = nFc - 3;

  for(int i = 5; i < mergeIndex && (i + 1) < nFc; ++i) {
    addEdge(out.rings[i], out.rings[i + 1], "R3");
  }
  for(int j = 0; (j + 1) < nGuard; ++j) {
    addEdge(out.guards[j], out.guards[j + 1], "R3");
  }

  if(mergeIndex >= 0 && mergeIndex < nFc) {
    addEdge(out.guards.back(), out.rings[mergeIndex], "R2");
  }

  for(int i = mergeIndex; (i + 1) < nFc; ++i) {
    addEdge(out.rings[i], out.rings[i + 1], "R1");
  }
  addEdge(out.rings.back(), "Cathode", "R_C");

  return out;
}

static YAML::Node toStringSequence(const std::vector<std::string> &values) {
  YAML::Node seq(YAML::NodeType::Sequence);
  for(const auto &v : values) seq.push_back(v);
  return seq;
}

static bool isGroundExteriorBoundaryName(const std::string &name) {
  return name == kFallbackBoundaryName;
}

static bool isAxisExteriorBoundaryName(const std::string &name) {
  return name == kAxisBoundaryName;
}

static bool isWallChargeBoundaryName(const std::string &name) {
  return name == kWallBoundaryName;
}

static std::optional<YAML::Node>
loadFieldCageNetworkSidecar(const std::filesystem::path &inputGeometrySource) {
  const std::filesystem::path dir = inputGeometrySource.parent_path();
  const std::string stem = inputGeometrySource.stem().string();
  const std::vector<std::filesystem::path> candidates = {
    dir / (stem + ".fieldcage.yaml"),
    dir / (stem + ".fieldcage.yml"),
    dir / (stem + "_fieldcage.yaml"),
    dir / (stem + "_fieldcage.yml"),
  };

  std::optional<std::filesystem::path> found;
  for(const auto &candidate : candidates) {
    std::error_code ec;
    if(!std::filesystem::exists(candidate, ec) || ec) continue;
    if(!std::filesystem::is_regular_file(candidate, ec) || ec) continue;
    if(found) {
      throw std::runtime_error(
        "multiple field-cage sidecars found for geometry source '" +
        inputGeometrySource.string() + "': '" + found->string() + "' and '" +
        candidate.string() + "'");
    }
    found = candidate;
  }
  if(!found) return std::nullopt;

  YAML::Node loaded = YAML::LoadFile(found->string());
  YAML::Node fc = loaded["fieldcage_network"] ? loaded["fieldcage_network"] : loaded;
  if(!fc || !fc.IsMap()) {
    throw std::runtime_error(
      "field-cage sidecar '" + found->string() +
      "' must be a mapping or contain a 'fieldcage_network' mapping.");
  }
  if(!fc["enabled"]) {
    fc["enabled"] = true;
  }
  return fc;
}

static void writeGeometryAutogenConfig(
  const std::string &outputPath,
  const std::string &inputGeometrySource,
  const std::string &inputGeometryFormat,
  const std::string &meshPath,
  const std::vector<std::string> &includedRootNames,
  const std::optional<double> &effectiveLiquidLevel,
  const std::string &liquidLevelSource,
  const BuildResult &result,
  const geometry::MeshingOptions &options) {

  YAML::Node root;
  root["schema_version"] = 1;
  root["kind"] = "geometry_mesh_autogen";
  root["geometry_id"] = std::filesystem::path(inputGeometrySource).stem().string();

  YAML::Node generation;
  generation["tool"] = "mesh_gmsh/gmsh_xao_regions";
  generation["input_geometry_source"] = inputGeometrySource;
  generation["input_geometry_format"] = inputGeometryFormat;
  generation["output_mesh"] = meshPath;
  generation["included_root_count"] = static_cast<int>(includedRootNames.size());
  generation["root_selection_mode"] = "all";
  {
    std::vector<std::string> includedPreview;
    const std::size_t previewCount = std::min<std::size_t>(includedRootNames.size(), 32);
    includedPreview.reserve(previewCount);
    for(std::size_t i = 0; i < previewCount; ++i) includedPreview.push_back(includedRootNames[i]);
    generation["included_roots_preview"] = toStringSequence(includedPreview);
  }
  if(effectiveLiquidLevel) {
    generation["liquid_level"] = *effectiveLiquidLevel;
  } else {
    generation["liquid_level"] = YAML::Node();
  }
  generation["liquid_level_source"] = liquidLevelSource;
  root["generation"] = generation;

  YAML::Node materials(YAML::NodeType::Map);
  YAML::Node boundaries(YAML::NodeType::Map);
  YAML::Node physicalGroups(YAML::NodeType::Map);

  for(const auto &[name, rec] : result.physicalGroups) {
    YAML::Node pg;
    pg["dim"] = rec.dim;
    pg["tag"] = rec.tag;
    pg["entity_count"] = static_cast<int>(rec.entityTags.size());
    physicalGroups[name] = pg;

    if(rec.dim == 2) {
      YAML::Node mat;
      mat["attr_id"] = rec.tag;
      materials[name] = mat;
    } else if(rec.dim == 1) {
      YAML::Node bdr;
      bdr["bdr_id"] = rec.tag;
      if(isAxisExteriorBoundaryName(name)) {
        bdr["type"] = "neumann";
        bdr["value"] = 0.0;
      } else if(isWallChargeBoundaryName(name)) {
        bdr["type"] = "neumann";
      } else {
        bdr["type"] = "dirichlet";
        if(isGroundExteriorBoundaryName(name)) {
          bdr["value"] = 0.0;
        }
      }
      boundaries[name] = bdr;
    }
  }

  root["materials"] = materials;
  root["boundaries"] = boundaries;
  root["physical_groups"] = physicalGroups;

  const std::filesystem::path inputSourcePath = std::filesystem::absolute(inputGeometrySource);
  if(options.fieldCageNetworkFromGeometry) {
    const std::optional<YAML::Node> sidecarFieldCage =
      loadFieldCageNetworkSidecar(inputSourcePath);
    if(sidecarFieldCage) {
      generation["fieldcage_network_source"] = "sidecar";
      generation["fieldcage_network_autogenerated"] = false;
      root["generation"] = generation;
      root["fieldcage_network"] = *sidecarFieldCage;
    } else {
      generation["fieldcage_network_source"] = "none";
      generation["fieldcage_network_autogenerated"] = false;
      root["generation"] = generation;
    }
  } else {
    generation["fieldcage_network_source"] = "disabled";
    generation["fieldcage_network_autogenerated"] = false;
    root["generation"] = generation;
  }

  std::ofstream out(outputPath);
  if(!out) {
    throw std::runtime_error("failed to open geometry config output: " + outputPath);
  }
  out << root;
  if(!out) {
    throw std::runtime_error("failed to write geometry config output: " + outputPath);
  }
}

static bool isFluidName(const std::string &name) {
  const std::string l = lowerCopy(name);
  return l == "lxe" || l == "gxe";
}

static bool isElectrodeName(const std::string &name) {
  const std::string l = lowerCopy(name);
  return l.find("electrode") != std::string::npos ||
         l.find("anode") != std::string::npos ||
         l.find("cathode") != std::string::npos ||
         l.find("gate") != std::string::npos;
}

static bool isPMTRoot(const std::string &name) {
  return name == "BottomPMTs" || name == "TopPMTs";
}

static bool isElectrodeMaterial(const std::string &material) {
  return lowerCopy(material) == "electrode";
}

static bool isPTFEMaterial(const std::string &material) {
  return lowerCopy(material) == "ptfe";
}

static std::string canonicalMaterialGroup(const std::string &material) {
  const std::string lowered = lowerCopy(material);
  if(lowered == "lxe") return "LXe";
  if(lowered == "gxe") return "GXe";
  if(lowered == "ptfe") return "PTFE";
  if(lowered == "electrode") return "ELECTRODE";
  return material;
}

static std::vector<std::string> collectRootNames(const std::vector<Component> &roots) {
  std::vector<std::string> names;
  names.reserve(roots.size());
  for(const auto &root : roots) {
    if(!root.name.empty()) names.push_back(root.name);
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

static bool isFragmentBuilderFailureMessage(const std::string &message) {
  const std::string lowered = lowerCopy(message);
  if(lowered.find("bopalgo_alertbuilderfailed") != std::string::npos) return true;
  if(lowered.find("alertbuilderfailed") != std::string::npos) return true;
  const bool hasFragmentWord = lowered.find("fragment") != std::string::npos;
  const bool hasFailureWord = lowered.find("failed") != std::string::npos;
  return hasFragmentWord && hasFailureWord;
}

static bool isBCObject(const std::string &name) {
  return name.rfind("BC_", 0) == 0;
}

static std::string normalizeBCName(std::string name) {
  if(name.empty()) return name;
  if(name.rfind("BC_", 0) == 0) return name;
  return "BC_" + name;
}

static RootClassification classifyRoot(const Component &root) {
  RootClassification out;
  if(lowerCopy(root.material) == "lxe" || root.name == "LXe") {
    out = RootClassification{"LXe", "", true, false};
  } else if(lowerCopy(root.material) == "gxe" || root.name == "GXe") {
    out = RootClassification{"GXe", "", true, false};
  } else if(isPTFEMaterial(root.material)) {
    out = RootClassification{"PTFE", "", true, false};
  } else if(!root.material.empty() && !isElectrodeMaterial(root.material)) {
    out = RootClassification{canonicalMaterialGroup(root.material), "", true, false};
  } else if(root.name == "FieldShapingRings" || root.name == "FieldShapingGuard") {
    // Keep split electrode BCs by default for these two bases.
    out = RootClassification{"ELECTRODE", "BC_" + root.name, false, true};
  } else if(isElectrodeMaterial(root.material) || isElectrodeName(root.name) || isPMTRoot(root.name)) {
    out = RootClassification{"ELECTRODE", "BC_" + root.name, false, false};
  } else {
    out = RootClassification{"OTHER", "", true, false};
  }

  const std::string explicitBCName = normalizeBCName(root.boundaryName);
  const bool flaggedBoundary = root.markBoundary || root.isElectrodeBoundary || root.groupBoundary;

  if(!explicitBCName.empty()) {
    out.bcName = explicitBCName;
  } else if(flaggedBoundary && out.bcName.empty()) {
    out.bcName = "BC_" + root.name;
  }

  if(root.splitBoundaryByIndex) {
    out.splitIndexedBC = true;
    if(out.bcName.empty()) out.bcName = "BC_" + root.name;
  }

  return out;
}

static void applyLiquidLevelToShrinkageRecursive(
  Component &component,
  double liquidLevel,
  int &candidates,
  int &updated) {

  if(component.applyShrinkage) {
    candidates++;
    if(!component.hasShrinkBelowY) {
      component.shrinkBelowY = liquidLevel;
      component.hasShrinkBelowY = true;
      updated++;
    }
  }

  for(auto &child : component.children) {
    applyLiquidLevelToShrinkageRecursive(child, liquidLevel, candidates, updated);
  }
}

class PointCache {
public:
  explicit PointCache(double tol = kCurveEndpointSnapTol) : tol_(tol) {}
  int getOrCreate(double x, double y, double z = 0.0, double meshSize = 0.0) {
    const Key key = quantize(x, y, z);
    // Probe neighboring bins: simple llround-based quantization alone can miss
    // near-equal points across bin boundaries.
    for(long long dx = -1; dx <= 1; ++dx) {
      for(long long dy = -1; dy <= 1; ++dy) {
        for(long long dz = -1; dz <= 1; ++dz) {
          const Key probe{key.x + dx, key.y + dy, key.z + dz};
          auto it = cache_.find(probe);
          if(it == cache_.end()) continue;
          for(const CacheEntry &entry : it->second) {
            if(samePoint(entry.x, entry.y, entry.z, x, y, z)) {
              return entry.tag;
            }
          }
        }
      }
    }

    int tag = gmsh::model::occ::addPoint(x, y, z, meshSize);
    cache_[key].push_back(CacheEntry{x, y, z, tag});
    return tag;
  }
private:
  struct Key { long long x, y, z; bool operator==(const Key &o) const { return x==o.x && y==o.y && z==o.z; } };
  struct Hasher { std::size_t operator()(const Key &k) const { return std::hash<long long>{}(k.x) ^ (std::hash<long long>{}(k.y)<<1) ^ (std::hash<long long>{}(k.z)<<2); } };
  struct CacheEntry {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    int tag = -1;
  };

  bool samePoint(double ax, double ay, double az,
                 double bx, double by, double bz) const {
    return std::abs(ax - bx) <= tol_ &&
           std::abs(ay - by) <= tol_ &&
           std::abs(az - bz) <= tol_;
  }

  Key quantize(double x, double y, double z) const {
    return Key{static_cast<long long>(std::llround(x / tol_)), static_cast<long long>(std::llround(y / tol_)), static_cast<long long>(std::llround(z / tol_))};
  }
  double tol_;
  std::unordered_map<Key, std::vector<CacheEntry>, Hasher> cache_;
};

class Builder {
public:
  Builder() : points_(kCurveEndpointSnapTol) {}

  BuildResult build(const std::vector<Component> &roots) {
    BuildResult result;

    sourceCurveOwner_.clear();
    finalCurveCandidates_.clear();
    rootBCIndexCounter_.clear();

    std::vector<std::pair<int,int>> fragmentInputs;
    for(const auto &c : roots) {
      const RootClassification rootClass = classifyRoot(c);
      buildComponentRecursive(c, c.name, rootClass, fragmentInputs);
    }
    gmsh::model::occ::synchronize();

    // only explicit dropping allowed here, before fragment
    pruneTinySourceSurfaces(fragmentInputs, 0.0);

    if(fragmentInputs.empty()) throw std::runtime_error("no valid source surfaces remain");

    std::vector<std::pair<int,int>> allFragmentInputs = fragmentInputs;
    for(const auto &[curveTag, owner] : sourceCurveOwner_) {
      allFragmentInputs.emplace_back(1, curveTag);
    }

    std::vector<std::pair<int,int>> outDimTags;
    std::vector<std::vector<std::pair<int,int>>> outDimTagsMap;
    gmsh::model::occ::fragment(allFragmentInputs, {}, outDimTags, outDimTagsMap, -1, true, true);
    gmsh::model::occ::synchronize();

    // Keep fragment provenance stable while we derive hole boundaries from the
    // resulting entities. Healing can change tags and break source tracking.
    std::vector<int> liveFaces;
    collectLiveFinalFaces(liveFaces);
    collectFinalFaceCandidates(allFragmentInputs, outDimTagsMap);
    collectFinalCurveCandidates(allFragmentInputs, outDimTagsMap);
    assignOwners(liveFaces, result.finalFaces, result.faceOwner);
    buildOwnerFaceMap(result.faceOwner, result.ownerFaces);
    buildExteriorBoundary(result.finalFaces, result.boundaryCurves);
    assignBoundaryOwners(result.boundaryCurves, result.bcCurvesByName);
    createPhysicalGroups(result);
    return result;
  }

private:
  struct SourceInfo {
    std::string name;
    std::string rootName;
    std::string materialGroup;
    std::string bcName;
    bool exportFaces = true;
    double area = 0.0;
  };
  PointCache points_;
  std::map<int, SourceInfo> sourceInfo_;
  std::map<int, std::set<int>> finalFaceCandidates_;
  std::unordered_map<int, std::string> sourceCurveOwner_;
  std::unordered_map<int, std::set<std::string>> finalCurveCandidates_;
  std::unordered_map<std::string, int> rootBCIndexCounter_;

  static void dedup(std::vector<int> &v) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); }

  static double safeSurfaceArea(int tag) {
    double mass = 0.0;
    try { gmsh::model::occ::getMass(2, tag, mass); return mass; } catch(...) { return 0.0; }
  }

  void collectFinalCurveCandidates(
    const std::vector<std::pair<int,int>> &allFragmentInputs,
    const std::vector<std::vector<std::pair<int,int>>> &outDimTagsMap) {

    finalCurveCandidates_.clear();

    for(std::size_t i = 0; i < allFragmentInputs.size() && i < outDimTagsMap.size(); ++i) {
      const auto &[dim, inTag] = allFragmentInputs[i];
      if(dim != 1) continue;

      auto itOwner = sourceCurveOwner_.find(inTag);
      if(itOwner == sourceCurveOwner_.end()) continue;
      const std::string &owner = itOwner->second;

      for(const auto &[odim, outTag] : outDimTagsMap[i]) {
        if(odim == 1) {
          finalCurveCandidates_[outTag].insert(owner);
        }
      }
    }
  }

  struct CurveBounds {
    double xmin = 0.0;
    double ymin = 0.0;
    double zmin = 0.0;
    double xmax = 0.0;
    double ymax = 0.0;
    double zmax = 0.0;
  };

  static bool getCurveBounds(const int curveTag, CurveBounds &out) {
    try {
      gmsh::model::getBoundingBox(
        1,
        curveTag,
        out.xmin,
        out.ymin,
        out.zmin,
        out.xmax,
        out.ymax,
        out.zmax);
      return true;
    } catch(...) {
      return false;
    }
  }

  static double estimateCurveCoordinateScale(
    const std::unordered_map<int, CurveBounds> &curveBounds) {
    double scale = 1.0;
    for(const auto &[curveTag, bounds] : curveBounds) {
      (void)curveTag;
      scale = std::max(scale, std::abs(bounds.xmin));
      scale = std::max(scale, std::abs(bounds.xmax));
      scale = std::max(scale, std::abs(bounds.ymin));
      scale = std::max(scale, std::abs(bounds.ymax));
      scale = std::max(scale, std::abs(bounds.zmin));
      scale = std::max(scale, std::abs(bounds.zmax));
    }
    return scale;
  }

  static bool isCurveOnAxisX0(const CurveBounds &bounds, const double tolX) {
    return std::abs(bounds.xmin) <= tolX && std::abs(bounds.xmax) <= tolX;
  }

  static bool isCurveNearConstantX(const CurveBounds &bounds, const double tolX) {
    return std::abs(bounds.xmax - bounds.xmin) <= tolX;
  }

  static double curveXSpan(const CurveBounds &bounds) {
    return std::abs(bounds.xmax - bounds.xmin);
  }

  static double curveYSpan(const CurveBounds &bounds) {
    return std::abs(bounds.ymax - bounds.ymin);
  }

  static bool isVerticalConstantXCurve(const CurveBounds &bounds,
                                       const double constantXTol,
                                       const double minVerticalSpan,
                                       const double minAspectRatio) {
    const double xspan = curveXSpan(bounds);
    const double yspan = curveYSpan(bounds);
    if(xspan > constantXTol) return false;
    if(yspan < minVerticalSpan) return false;
    const double denom = std::max(xspan, 0.25 * constantXTol);
    return yspan >= minAspectRatio * denom;
  }

  static double representativeX(const CurveBounds &bounds) {
    return 0.5 * (bounds.xmin + bounds.xmax);
  }

  void splitAxisAndWallFromFallbackBoundary(
    std::map<std::string, std::vector<int>> &bcCurvesByName) const {
    auto itFallback = bcCurvesByName.find(kFallbackBoundaryName);
    if(itFallback == bcCurvesByName.end()) return;

    std::vector<int> fallbackCurves = itFallback->second;
    dedup(fallbackCurves);
    if(fallbackCurves.empty()) {
      bcCurvesByName.erase(itFallback);
      return;
    }

    std::unordered_map<int, CurveBounds> curveBounds;
    curveBounds.reserve(fallbackCurves.size());
    for(const int curveTag : fallbackCurves) {
      CurveBounds bounds;
      if(getCurveBounds(curveTag, bounds)) {
        curveBounds.emplace(curveTag, bounds);
      }
    }

    const double scale = estimateCurveCoordinateScale(curveBounds);
    // Be tolerant to CAD/import noise: axis/wall splits should not require
    // numerically exact x-constancy to the nanometer.
    const double tolX = std::max(1e-7, 5e-6 * scale);
    const double constantXTol = std::max(tolX, 2e-5 * scale);
    const double wallTolX = std::max(constantXTol, 2e-4 * scale);
    const double minVerticalSpan = std::max(10.0 * tolX, 1e-6 * scale);
    constexpr double minVerticalAspect = 4.0;

    double wallMinX = std::numeric_limits<double>::infinity();
    for(const int curveTag : fallbackCurves) {
      const auto itBounds = curveBounds.find(curveTag);
      if(itBounds == curveBounds.end()) continue;
      const CurveBounds &bounds = itBounds->second;
      if(isCurveOnAxisX0(bounds, tolX)) continue;
      if(!isVerticalConstantXCurve(
           bounds, constantXTol, minVerticalSpan, minVerticalAspect)) {
        continue;
      }
      wallMinX = std::min(wallMinX, representativeX(bounds));
    }

    std::vector<int> axisCurves;
    std::vector<int> wallCurves;
    std::vector<int> remainingCurves;
    axisCurves.reserve(fallbackCurves.size());
    wallCurves.reserve(fallbackCurves.size());
    remainingCurves.reserve(fallbackCurves.size());

    for(const int curveTag : fallbackCurves) {
      const auto itBounds = curveBounds.find(curveTag);
      if(itBounds == curveBounds.end()) {
        remainingCurves.push_back(curveTag);
        continue;
      }

      const CurveBounds &bounds = itBounds->second;
      if(isCurveOnAxisX0(bounds, tolX)) {
        axisCurves.push_back(curveTag);
      } else if(
        std::isfinite(wallMinX) &&
        isVerticalConstantXCurve(
          bounds, constantXTol, minVerticalSpan, minVerticalAspect) &&
        std::abs(representativeX(bounds) - wallMinX) <= wallTolX) {
        wallCurves.push_back(curveTag);
      } else {
        remainingCurves.push_back(curveTag);
      }
    }

    dedup(axisCurves);
    dedup(wallCurves);
    dedup(remainingCurves);

    itFallback->second = std::move(remainingCurves);
    if(itFallback->second.empty()) {
      bcCurvesByName.erase(itFallback);
    }

    if(!axisCurves.empty()) {
      auto &axisGroup = bcCurvesByName[kAxisBoundaryName];
      axisGroup.insert(axisGroup.end(), axisCurves.begin(), axisCurves.end());
      dedup(axisGroup);
    }
    if(!wallCurves.empty()) {
      auto &wallGroup = bcCurvesByName[kWallBoundaryName];
      wallGroup.insert(wallGroup.end(), wallCurves.begin(), wallCurves.end());
      dedup(wallGroup);
    }

    debugLog(
      "[boundary split] fallback='",
      kFallbackBoundaryName,
      "' axis='",
      kAxisBoundaryName,
      "' wall='",
      kWallBoundaryName,
      "' axis_curves=",
      axisCurves.size(),
      " wall_curves=",
      wallCurves.size(),
      " remaining_fallback_curves=",
      (bcCurvesByName.count(kFallbackBoundaryName)
         ? bcCurvesByName.at(kFallbackBoundaryName).size()
         : 0),
      " tol_axis_x=",
      tolX,
      " tol_const_x=",
      constantXTol,
      " wall_tol_x=",
      wallTolX,
      " min_vertical_span=",
      minVerticalSpan,
      " min_vertical_aspect=",
      minVerticalAspect,
      " wall_min_x=",
      (std::isfinite(wallMinX) ? wallMinX : std::numeric_limits<double>::quiet_NaN()),
      "\n");
  }

  void assignBoundaryOwners(const std::vector<int> &boundaryCurves,
                          std::map<std::string, std::vector<int>> &bcCurvesByName) const {
    bcCurvesByName.clear();

    for(int curve : boundaryCurves) {
      std::string owner = kFallbackBoundaryName;

      auto it = finalCurveCandidates_.find(curve);
      if(it != finalCurveCandidates_.end()) {
        std::vector<std::string> bcOwners;
        for(const auto &cand : it->second) {
          if(isBCObject(cand)) bcOwners.push_back(cand);
        }
        if(!bcOwners.empty()) {
          std::sort(bcOwners.begin(), bcOwners.end());
          owner = bcOwners.front();
        }
      }

      bcCurvesByName[owner].push_back(curve);
    }

    for(auto &[name, tags] : bcCurvesByName) {
      dedup(tags);
    }
    splitAxisAndWallFromFallbackBoundary(bcCurvesByName);
  }

  void buildComponentRecursive(const Component &component,
                               const std::string &rootName,
                               const RootClassification &rootClass,
                               std::vector<std::pair<int,int>> &fragmentInputs) {
    if(!component.hullOnly && !component.outer.empty()) {
      auto expanded = expand(component);
      for(const auto &instance : expanded) {
        std::string instanceBCName = rootClass.bcName;
        if(rootClass.splitIndexedBC && !rootClass.bcName.empty()) {
          const int idx = rootBCIndexCounter_[rootName]++;
          instanceBCName = rootClass.bcName + "_" + std::to_string(idx);
        }

        int s = -1;
        try {
          s = buildSurface(instance, instanceBCName);
        } catch(const std::exception &e) {
          debugLog(
            "[build warning] skipping component '",
            instance.name,
            "' reason: ",
            e.what(),
            "\n");
          continue;
        } catch(...) {
          debugLog(
            "[build warning] skipping component '",
            instance.name,
            "' reason: unknown non-std exception\n");
          continue;
        }

        sourceInfo_[s] = SourceInfo{
          instance.name,
          rootName,
          rootClass.materialGroup,
          instanceBCName,
          rootClass.exportFaces,
          safeSurfaceArea(s)
        };
        fragmentInputs.push_back({2, s});
      }
    }
    for(const auto &child : component.children) {
      buildComponentRecursive(child, rootName, rootClass, fragmentInputs);
    }
  }

  std::vector<Component> expand(const Component &component) {
    std::vector<Component> out;
    RepeatSpec rep = component.repeat.value_or(RepeatSpec{});
    int n = std::max(1, rep.number);
    for(int i=0;i<n;++i) {
      Component c = component;
      if(n > 1) c.name = component.name + "_" + std::to_string(i);
      translate(c, rep.dx * i, rep.dy * i);
      if(c.applyShrinkage && std::abs(c.shrinkageFactor - 1.0) > 1e-12) shrink(c);
      out.push_back(std::move(c));
    }
    return out;
  }

  static void movePoint(Vec2 &p, double dx, double dy) { p.x += dx; p.y += dy; }
  static bool samePoint(const Vec2 &a, const Vec2 &b, double tol = 1e-9) {
    return std::abs(a.x - b.x) <= tol && std::abs(a.y - b.y) <= tol;
  }

  static void translate(Component &c, double dx, double dy) {
    auto moveSeg = [&](CurveSegment &s) {
      movePoint(s.start, dx, dy); movePoint(s.end, dx, dy);
      if(s.center) movePoint(*s.center, dx, dy);
      for(auto &p : s.poles) movePoint(p, dx, dy);
    };
    for(auto &s : c.outer) moveSeg(s);
    for(auto &h : c.holes) for(auto &s : h) moveSeg(s);
    for(auto &child : c.children) translate(child, dx, dy);
  }
  static void shrink(Component &c) {
    auto shrinkPoint = [&](Vec2 &p){ if(p.y < c.shrinkBelowY) p.y *= c.shrinkageFactor; };
    auto shrinkSeg = [&](CurveSegment &s){ shrinkPoint(s.start); shrinkPoint(s.end); if(s.center) shrinkPoint(*s.center); for(auto &p : s.poles) shrinkPoint(p); };
    for(auto &s : c.outer) shrinkSeg(s);
    for(auto &h : c.holes) for(auto &s : h) shrinkSeg(s);
  }

  int addCurve(const CurveSegment &seg) {
    int p0 = points_.getOrCreate(seg.start.x, seg.start.y, 0.0);
    int p1 = points_.getOrCreate(seg.end.x, seg.end.y, 0.0);
    switch(seg.kind) {
      case CurveSegment::Kind::Line:
        return gmsh::model::occ::addLine(p0, p1);
      case CurveSegment::Kind::Arc: {
        if(!seg.center) throw std::runtime_error("arc missing center");
        const double a0 =
          std::atan2(seg.start.y - seg.center->y, seg.start.x - seg.center->x);
        const double a1 =
          std::atan2(seg.end.y - seg.center->y, seg.end.x - seg.center->x);
        double delta = a1 - a0;
        if(seg.orientation) {
          if(delta <= 0.0) delta += 2.0 * M_PI;
        } else {
          if(delta >= 0.0) delta -= 2.0 * M_PI;
        }
        const double amid = a0 + 0.5 * delta;
        const double radius =
          std::hypot(seg.start.x - seg.center->x, seg.start.y - seg.center->y);
        Vec2 mid{
          seg.center->x + radius * std::cos(amid),
          seg.center->y + radius * std::sin(amid),
        };
        int pm = points_.getOrCreate(mid.x, mid.y, 0.0);
        return gmsh::model::occ::addCircleArc(p0, pm, p1, -1, false);
      }
      case CurveSegment::Kind::EllipseArc: {
        if(!seg.center) throw std::runtime_error("ellipse missing center");
        double A = seg.a, B = seg.b; if(B > A) std::swap(A, B);
        double c = std::sqrt(std::max(0.0, A*A - B*B));
        double phi = seg.phiDeg * M_PI / 180.0;
        Vec2 f{seg.center->x + c*std::cos(phi), seg.center->y + c*std::sin(phi)};
        int pc = points_.getOrCreate(seg.center->x, seg.center->y, 0.0);
        int pf = points_.getOrCreate(f.x, f.y, 0.0);
        return gmsh::model::occ::addEllipseArc(p0, pc, pf, p1);
      }
      case CurveSegment::Kind::BSpline:
      case CurveSegment::Kind::HyperbolaNurbs: {
        std::vector<int> pts;
        if(seg.poles.empty()) throw std::runtime_error("spline without poles");
        for(const auto &p : seg.poles) pts.push_back(points_.getOrCreate(p.x, p.y, 0.0));
        return gmsh::model::occ::addBSpline(pts, -1, seg.degree, seg.weights, seg.knots, seg.multiplicities);
      }
    }
    throw std::runtime_error("unsupported curve kind");
  }

  std::vector<CurveSegment> orderSegmentsForLoop(const std::vector<CurveSegment> &segments,
                                                 const std::string &loopLabel) const {
    if(segments.size() <= 1) return segments;

    std::vector<CurveSegment> bestOrdered;
    std::size_t bestUsedCount = 0;

    for(std::size_t startIdx = 0; startIdx < segments.size(); ++startIdx) {
      std::vector<CurveSegment> ordered;
      std::vector<bool> used(segments.size(), false);
      ordered.push_back(segments[startIdx]);
      used[startIdx] = true;

      Vec2 currentEnd = segments[startIdx].end;
      while(!samePoint(currentEnd, ordered.front().start)) {
        std::size_t nextIdx = segments.size();
        for(std::size_t i = 0; i < segments.size(); ++i) {
          if(used[i]) continue;
          if(!samePoint(segments[i].start, currentEnd)) continue;
          if(nextIdx == segments.size()) nextIdx = i;
          if(!samePoint(segments[i].end, ordered.front().start)) {
            nextIdx = i;
            break;
          }
        }
        if(nextIdx == segments.size()) break;

        ordered.push_back(segments[nextIdx]);
        used[nextIdx] = true;
        currentEnd = segments[nextIdx].end;
      }

      if(!samePoint(currentEnd, ordered.front().start)) continue;

      const std::size_t usedCount =
        static_cast<std::size_t>(std::count(used.begin(), used.end(), true));
      if(usedCount > bestUsedCount) {
        bestOrdered = ordered;
        bestUsedCount = usedCount;
      }
      if(bestUsedCount == segments.size()) break;
    }

    if(!bestOrdered.empty()) {
      if(bestUsedCount != segments.size()) {
        debugLog(
          "[loop reorder] ",
          loopLabel,
          " using ",
          bestUsedCount,
          "/",
          segments.size(),
          " segments; ignoring duplicate/unreachable entries\n");
      }
      return bestOrdered;
    }

    return segments;
  }

  int buildLoop(const std::vector<CurveSegment> &segments,
                const std::string &bcName) {
    if(segments.empty()) throw std::runtime_error("empty contour");

    const std::vector<CurveSegment> orderedSegments =
      orderSegmentsForLoop(segments, bcName.empty() ? "material" : bcName);

    std::vector<int> curves;
    for(const auto &s : orderedSegments) {
      int curveTag = addCurve(s);
      curves.push_back(curveTag);

      if(!bcName.empty()) {
        sourceCurveOwner_[curveTag] = bcName;
      }
    }

    return gmsh::model::occ::addCurveLoop(curves);
  }

  int buildSurface(const Component &component, const std::string &bcName) {
    int outer = buildLoop(component.outer, bcName);
    std::vector<int> loops{outer};
    for(const auto &h : component.holes) {
      loops.push_back(buildLoop(h, bcName));
    }
    return gmsh::model::occ::addPlaneSurface(loops);
  }

  void pruneTinySourceSurfaces(std::vector<std::pair<int,int>> &fragmentInputs, double minArea) {
    if(minArea <= 0.0) return;
    std::vector<std::pair<int,int>> kept;
    for(const auto &dt : fragmentInputs) {
      if(dt.first != 2) continue;
      double area = safeSurfaceArea(dt.second);
      if(area < minArea) {
        auto it = sourceInfo_.find(dt.second);
        debugLog(
          "[pre-fragment drop] surface=",
          dt.second,
          " name='",
          (it == sourceInfo_.end() ? std::string("?") : it->second.name),
          "' area=",
          area,
          "\n");
        continue;
      }
      kept.push_back(dt);
    }
    fragmentInputs.swap(kept);
  }

  void applyPostFragmentFixing() {
    gmsh::option::setNumber("Geometry.OCCFixSmallEdges", 1);
    gmsh::option::setNumber("Geometry.OCCFixSmallFaces", 1);
    gmsh::option::setNumber("Geometry.OCCFixDegenerated", 1);
    try {
      std::vector<std::pair<int,int>> healed, toHeal;
      gmsh::model::getEntities(toHeal, 2);
      gmsh::model::occ::healShapes(healed, toHeal, 1e-8, true, true, true, false, false);
    } catch(const std::exception &e) {
      debugLog("[heal warning] ", e.what(), "\n");
    } catch(...) {
      debugLog("[heal warning] unknown exception\n");
    }
    gmsh::model::occ::synchronize();
  }

  void collectLiveFinalFaces(std::vector<int> &out) {
    out.clear();
    std::vector<std::pair<int,int>> ents;
    gmsh::model::getEntities(ents, 2);
    for(const auto &dt : ents) if(dt.first == 2) out.push_back(dt.second);
    dedup(out);
  }

  void collectFinalFaceCandidates(const std::vector<std::pair<int,int>> &fragmentInputs,
                                  const std::vector<std::vector<std::pair<int,int>>> &outDimTagsMap) {
    finalFaceCandidates_.clear();
    for(std::size_t i = 0; i < fragmentInputs.size() && i < outDimTagsMap.size(); ++i) {
      int srcTag = fragmentInputs[i].second;
      auto sit = sourceInfo_.find(srcTag);
      if(sit == sourceInfo_.end()) continue;
      for(const auto &dt : outDimTagsMap[i]) {
        if(dt.first != 2) continue;
        finalFaceCandidates_[dt.second].insert(srcTag);
      }
    }
  }

  std::optional<std::string> resolveOwner(int finalFace) const {
    auto it = finalFaceCandidates_.find(finalFace);
    if(it == finalFaceCandidates_.end() || it->second.empty()) return std::string("OTHER");

    std::set<std::string> materialGroups;
    std::set<std::string> rootNames;
    bool hasExportableFace = false;
    for(int srcTag : it->second) {
      auto sit = sourceInfo_.find(srcTag);
      if(sit == sourceInfo_.end()) continue;
      materialGroups.insert(sit->second.materialGroup);
      rootNames.insert(sit->second.rootName);
      hasExportableFace = hasExportableFace || sit->second.exportFaces;
    }

    if(materialGroups.count("ELECTRODE")) return std::nullopt;

    std::set<std::string> solidMaterials;
    for(const std::string &group : materialGroups) {
      if(group.empty()) continue;
      if(group == "ELECTRODE" || group == "LXe" || group == "GXe" || group == "OTHER") continue;
      solidMaterials.insert(group);
    }
    if(solidMaterials.size() == 1) {
      return *solidMaterials.begin();
    }
    if(solidMaterials.size() > 1) {
      std::ostringstream oss;
      bool first = true;
      for(const std::string &group : solidMaterials) {
        if(!first) oss << ", ";
        first = false;
        oss << group;
      }
      throw std::runtime_error(
        "surface " + std::to_string(finalFace) +
        " matched multiple solid materials; geometry ownership is ambiguous: " +
        oss.str());
    }

    if(materialGroups.count("LXe") && !materialGroups.count("GXe")) return std::string("LXe");
    if(materialGroups.count("GXe") && !materialGroups.count("LXe")) return std::string("GXe");

    if(materialGroups.count("LXe") && materialGroups.count("GXe")) {
      throw std::runtime_error(
        "surface " + std::to_string(finalFace) +
        " matched both LXe and GXe; geometry ownership is ambiguous");
    }

    if(materialGroups.count("OTHER") || hasExportableFace) return std::string("OTHER");
    return std::nullopt;
  }

  void assignOwners(const std::vector<int> &faces,
                    std::vector<int> &keptFaces,
                    std::map<int,std::string> &faceOwner) {
    keptFaces.clear();
    faceOwner.clear();
    for(int tag : faces) {
      auto owner = resolveOwner(tag);

      auto it = finalFaceCandidates_.find(tag);
      std::vector<std::string> candidates;
      if(it != finalFaceCandidates_.end()) {
        for(int srcTag : it->second) {
          auto sit = sourceInfo_.find(srcTag);
          if(sit == sourceInfo_.end()) continue;
          std::ostringstream label;
          label << sit->second.name << " root=" << sit->second.rootName
                << " material=" << sit->second.materialGroup;
          if(!sit->second.bcName.empty()) label << " bc=" << sit->second.bcName;
          candidates.push_back(label.str());
        }
        std::sort(candidates.begin(), candidates.end());
      }

      if(owner) {
        faceOwner[tag] = *owner;
        keptFaces.push_back(tag);
        debugLog("[fragment owner] surface ", tag, " -> '", faceOwner[tag], "'");
      } else {
        debugLog("[fragment owner] surface ", tag, " -> omitted_2d");
      }

      if(!candidates.empty()) {
        debugLog(" candidates={");
        for(std::size_t i = 0; i < candidates.size(); ++i) {
          if(i) debugLog(", ");
          debugLog(candidates[i]);
        }
        debugLog("}");
      }
      debugLog("\n");
    }
    dedup(keptFaces);
  }

  void buildOwnerFaceMap(const std::map<int,std::string> &faceOwner, std::map<std::string,std::vector<int>> &ownerFaces) {
    ownerFaces.clear();
    for(const auto &[face, owner] : faceOwner) ownerFaces[owner].push_back(face);
    for(auto &[name, faces] : ownerFaces) dedup(faces);
  }

  void buildExteriorBoundary(const std::vector<int> &faces, std::vector<int> &curves) {
    curves.clear();
    std::vector<std::pair<int,int>> input;
    for(int f : faces) input.push_back({2, f});
    std::vector<std::pair<int,int>> bd;
    gmsh::model::getBoundary(input, bd, true, false, false);
    for(const auto &dt : bd) if(dt.first == 1) curves.push_back(dt.second);
    dedup(curves);
    debugLog(
      "[material boundary] faces=",
      faces.size(),
      " curves=",
      curves.size(),
      "\n");
  }

  void createPhysicalGroups(BuildResult &result) {
    auto add = [&](int dim, const std::string &name, const std::vector<int> &tags) {
      if(tags.empty()) return;
      int pg = gmsh::model::addPhysicalGroup(dim, tags);
      gmsh::model::setPhysicalName(dim, pg, name);
      result.physicalGroups[name] = PhysicalGroupRecord{dim, pg, name, tags};
    };

    // export each final face exactly once under its resolved owner name
    for(const auto &[owner, faces] : result.ownerFaces) add(2, owner, faces);
    for(const auto &[bcName, tags] : result.bcCurvesByName) {
      std::vector<int> clean = tags;
      dedup(clean);
      if(clean.empty()) continue;

      debugLog("[bc group] ", bcName, " curves=", clean.size(), "\n");
      int pg = gmsh::model::addPhysicalGroup(1, clean);
      gmsh::model::setPhysicalName(1, pg, bcName);
      result.physicalGroups[bcName] = PhysicalGroupRecord{1, pg, bcName, clean};
    }
  }
}; // --- class Builder

static bool checkNonManifoldTriangleEdges() {
  using Edge = std::pair<std::size_t, std::size_t>;
  std::map<Edge, std::vector<std::size_t>> edgeToElems;
  std::vector<int> types;
  std::vector<std::vector<std::size_t>> elemTags, nodeTags;
  gmsh::model::mesh::getElements(types, elemTags, nodeTags, 2);
  for(std::size_t t=0; t<types.size(); ++t) {
    if(types[t] != 2) continue;
    const auto &elems = elemTags[t];
    const auto &nodes = nodeTags[t];
    for(std::size_t i=0; i<elems.size(); ++i) {
      std::size_t n1 = nodes[3*i+0], n2 = nodes[3*i+1], n3 = nodes[3*i+2];
      std::array<Edge,3> edges = { std::minmax(n1,n2), std::minmax(n2,n3), std::minmax(n3,n1) };
      for(const auto &e : edges) edgeToElems[e].push_back(elems[i]);
    }
  }
  int bad = 0;
  for(const auto &[edge, elems] : edgeToElems) {
    (void)edge;
    if(elems.size() > 2) {
      ++bad;
    }
  }
  debugLog("[check] non-manifold edges=", bad, "\n");
  return bad > 0;
}

static bool identifyFaulty1DSegments() {
  using NodeId = std::size_t;
  using ElemId = std::size_t;
  using Edge = std::pair<NodeId, NodeId>;

  std::map<Edge, std::vector<ElemId>> edgeTo2DElems;

  std::vector<int> types2d;
  std::vector<std::vector<std::size_t>> elemTags2d, nodeTags2d;
  gmsh::model::mesh::getElements(types2d, elemTags2d, nodeTags2d, 2);

  for(std::size_t t = 0; t < types2d.size(); ++t) {
    if(types2d[t] != 2) continue; // linear triangles only

    const auto &elems = elemTags2d[t];
    const auto &nodes = nodeTags2d[t];

    for(std::size_t i = 0; i < elems.size(); ++i) {
      NodeId n1 = nodes[3 * i + 0];
      NodeId n2 = nodes[3 * i + 1];
      NodeId n3 = nodes[3 * i + 2];

      std::array<Edge, 3> edges = {
        std::minmax(n1, n2),
        std::minmax(n2, n3),
        std::minmax(n3, n1)
      };

      for(const auto &e : edges) {
        edgeTo2DElems[e].push_back(elems[i]);
      }
    }
  }

  std::vector<int> types1d;
  std::vector<std::vector<std::size_t>> elemTags1d, nodeTags1d;
  gmsh::model::mesh::getElements(types1d, elemTags1d, nodeTags1d, 1);

  int faulty = 0;
  int exteriorLike = 0;
  int interfaceLike = 0;

  for(std::size_t t = 0; t < types1d.size(); ++t) {
    if(types1d[t] != 1) continue; // linear segments only

    const auto &elems = elemTags1d[t];
    const auto &nodes = nodeTags1d[t];

    for(std::size_t i = 0; i < elems.size(); ++i) {
      ElemId seg = elems[i];
      Edge edge = std::minmax(nodes[2 * i + 0], nodes[2 * i + 1]);

      auto it = edgeTo2DElems.find(edge);
      std::size_t adj = (it == edgeTo2DElems.end()) ? 0 : it->second.size();

      int elemType = -1, dim = -1, entityTag = -1;
      std::vector<std::size_t> segNodes;
      gmsh::model::mesh::getElement(seg, elemType, segNodes, dim, entityTag);

      std::vector<int> physicalTags;
      try {
        gmsh::model::getPhysicalGroupsForEntity(dim, entityTag, physicalTags);
      } catch(...) {
        physicalTags.clear();
      }

      // Skip untagged 1D entities entirely; we only care about exported BCs.
      if(physicalTags.empty()) continue;

      if(adj == 1) {
        ++exteriorLike;
        continue;
      }

      if(adj == 2) {
        ++interfaceLike;
        continue;
      }

      ++faulty;

    }
  }
  debugLog(
    "[check] tagged_1d exterior=",
    exteriorLike,
    " interface=",
    interfaceLike,
    " faulty=",
    faulty,
    "\n");
  return faulty > 0;
}

static bool checkTriangleOrientation() {
  using NodeId = std::size_t;
  using ElemId = std::size_t;

  std::vector<int> types;
  std::vector<std::vector<std::size_t>> elemTags, nodeTags;
  gmsh::model::mesh::getElements(types, elemTags, nodeTags, 2);

  std::vector<NodeId> allNodes;
  for(std::size_t t = 0; t < types.size(); ++t) {
    if(types[t] != 2) continue; // 3-node linear triangles only
    const auto &nodes = nodeTags[t];
    allNodes.insert(allNodes.end(), nodes.begin(), nodes.end());
  }

  std::sort(allNodes.begin(), allNodes.end());
  allNodes.erase(std::unique(allNodes.begin(), allNodes.end()), allNodes.end());

  std::vector<double> coords, params;
  gmsh::model::mesh::getNodes(allNodes, coords, params);

  std::map<NodeId, std::array<double, 3>> xyz;
  for(std::size_t i = 0; i < allNodes.size(); ++i) {
    xyz[allNodes[i]] = {
      coords[3 * i + 0],
      coords[3 * i + 1],
      coords[3 * i + 2]
    };
  }

  int positive = 0;
  int negative = 0;
  int degenerate = 0;
  const double eps = 1e-14;

  for(std::size_t t = 0; t < types.size(); ++t) {
    if(types[t] != 2) continue;

    const auto &elems = elemTags[t];
    const auto &nodes = nodeTags[t];

    for(std::size_t i = 0; i < elems.size(); ++i) {
      ElemId e = elems[i];

      NodeId n1 = nodes[3 * i + 0];
      NodeId n2 = nodes[3 * i + 1];
      NodeId n3 = nodes[3 * i + 2];

      const auto &p1 = xyz[n1];
      const auto &p2 = xyz[n2];
      const auto &p3 = xyz[n3];

      double signed2A =
        (p2[0] - p1[0]) * (p3[1] - p1[1]) -
        (p3[0] - p1[0]) * (p2[1] - p1[1]);

      if(signed2A > eps) {
        positive++;
      }
      else if(signed2A < -eps) {
        negative++;
      }
      else {
        degenerate++;
      }
    }
  }

  debugLog(
    "[check] orientation positive=",
    positive,
    " negative=",
    negative,
    " degenerate=",
    degenerate,
    "\n");

  // Orientation is valid iff all non-degenerate triangles share one winding.
  return !(((positive == 0) || (negative == 0)) && (degenerate == 0));
}

static void fixSurfaceMeshOrientation() {
  using NodeId = std::size_t;
  using ElemId = std::size_t;

  std::vector<int> types;
  std::vector<std::vector<std::size_t>> elemTags, nodeTags;
  gmsh::model::mesh::getElements(types, elemTags, nodeTags, 2);

  std::vector<NodeId> allNodes;
  for(std::size_t t = 0; t < types.size(); ++t) {
    if(types[t] != 2) continue; // linear triangles
    allNodes.insert(allNodes.end(), nodeTags[t].begin(), nodeTags[t].end());
  }

  std::sort(allNodes.begin(), allNodes.end());
  allNodes.erase(std::unique(allNodes.begin(), allNodes.end()), allNodes.end());

  std::vector<double> coords, params;
  gmsh::model::mesh::getNodes(allNodes, coords, params);

  std::map<NodeId, std::array<double, 3>> xyz;
  for(std::size_t i = 0; i < allNodes.size(); ++i) {
    xyz[allNodes[i]] = {
      coords[3 * i + 0],
      coords[3 * i + 1],
      coords[3 * i + 2]
    };
  }

  struct Counts {
    int pos = 0;
    int neg = 0;
  };
  std::map<int, Counts> surfCounts;

  const double eps = 1e-14;

  for(std::size_t t = 0; t < types.size(); ++t) {
    if(types[t] != 2) continue;

    const auto &elems = elemTags[t];
    const auto &nodes = nodeTags[t];

    for(std::size_t i = 0; i < elems.size(); ++i) {
      ElemId e = elems[i];
      NodeId n1 = nodes[3 * i + 0];
      NodeId n2 = nodes[3 * i + 1];
      NodeId n3 = nodes[3 * i + 2];

      const auto &p1 = xyz[n1];
      const auto &p2 = xyz[n2];
      const auto &p3 = xyz[n3];

      double signed2A =
        (p2[0] - p1[0]) * (p3[1] - p1[1]) -
        (p3[0] - p1[0]) * (p2[1] - p1[1]);

      int elemType = -1;
      int dim = -1;
      int entityTag = -1;
      std::vector<std::size_t> eNodes;
      gmsh::model::mesh::getElement(e, elemType, eNodes, dim, entityTag);

      if(dim != 2) continue;

      if(signed2A > eps) surfCounts[entityTag].pos++;
      else if(signed2A < -eps) surfCounts[entityTag].neg++;
    }
  }

  std::vector<std::pair<int, int>> toReverse;
  for(const auto &[surfTag, c] : surfCounts) {
    if(c.neg > c.pos) {
      debugLog(
        "[reverse surface] tag=",
        surfTag,
        " pos=",
        c.pos,
        " neg=",
        c.neg,
        "\n");
      toReverse.push_back({2, surfTag});
    }
  }

  if(!toReverse.empty()) {
    gmsh::model::mesh::reverse(toReverse);
  }
}

static std::size_t countTriangleElements() {
  std::vector<int> types;
  std::vector<std::vector<std::size_t>> elemTags, nodeTags;
  gmsh::model::mesh::getElements(types, elemTags, nodeTags, 2);
  std::size_t count = 0;
  for(std::size_t i = 0; i < types.size(); ++i) {
    if(types[i] != 2) continue; // 3-node triangle
    count += elemTags[i].size();
  }
  return count;
}

static void validateBuildResult(const BuildResult &result) {
  if(result.finalFaces.empty()) {
    throw std::runtime_error("geometry build produced no final faces");
  }
  if(result.boundaryCurves.empty()) {
    throw std::runtime_error("geometry build produced no boundary curves");
  }

  auto hasNonEmptyPhysicalGroup = [&](const std::string &name, int dim) {
    auto it = result.physicalGroups.find(name);
    if(it == result.physicalGroups.end()) return false;
    return it->second.dim == dim && !it->second.entityTags.empty();
  };

  if(!hasNonEmptyPhysicalGroup("LXe", 2)) {
    throw std::runtime_error("missing or empty LXe physical surface group");
  }
  if(!hasNonEmptyPhysicalGroup("GXe", 2)) {
    throw std::runtime_error("missing or empty GXe physical surface group");
  }

  bool hasBoundaryGroups = false;
  for(const auto &[name, rec] : result.physicalGroups) {
    (void)name;
    if(rec.dim != 1) continue;
    if(rec.entityTags.empty()) continue;
    hasBoundaryGroups = true;
    break;
  }
  if(!hasBoundaryGroups) {
    throw std::runtime_error("no non-empty 1D boundary physical groups were produced");
  }

  std::vector<std::string> missingRequired;
  if(!hasNonEmptyPhysicalGroup(kAxisBoundaryName, 1)) {
    missingRequired.push_back(kAxisBoundaryName);
  }
  if(!hasNonEmptyPhysicalGroup(kWallBoundaryName, 1)) {
    missingRequired.push_back(kWallBoundaryName);
  }

  if(!missingRequired.empty()) {
    std::vector<std::string> available1D;
    for(const auto &[name, rec] : result.physicalGroups) {
      if(rec.dim != 1) continue;
      if(rec.entityTags.empty()) continue;
      std::ostringstream entry;
      entry << name << "(curves=" << rec.entityTags.size() << ")";
      available1D.push_back(entry.str());
    }
    std::sort(available1D.begin(), available1D.end());

    std::ostringstream oss;
    oss << "required exterior boundary groups missing: ";
    for(std::size_t i = 0; i < missingRequired.size(); ++i) {
      if(i) oss << ", ";
      oss << missingRequired[i];
    }
    oss << ". Mesher requires both '" << kAxisBoundaryName << "' and '"
        << kWallBoundaryName << "'. "
        << "Fallback outer-boundary group name is '" << kFallbackBoundaryName
        << "'. ";
    if(available1D.empty()) {
      oss << "No non-empty 1D groups available.";
    } else {
      oss << "Available non-empty 1D groups: ";
      for(std::size_t i = 0; i < available1D.size(); ++i) {
        if(i) oss << ", ";
        oss << available1D[i];
      }
      oss << ".";
    }
    throw std::runtime_error(oss.str());
  }
}

static std::size_t countElementsInDimension(int dim) {
  std::vector<int> types;
  std::vector<std::vector<std::size_t>> elemTags, nodeTags;
  gmsh::model::mesh::getElements(types, elemTags, nodeTags, dim);
  std::size_t count = 0;
  for(std::size_t i = 0; i < elemTags.size(); ++i) {
    count += elemTags[i].size();
  }
  return count;
}

static std::vector<double> toDoubleList(const std::vector<int> &tags) {
  std::vector<double> out;
  out.reserve(tags.size());
  for(const int tag : tags) {
    out.push_back(static_cast<double>(tag));
  }
  return out;
}

static void applyMeshingOptions(const geometry::MeshingOptions &options) {
  const std::map<std::string, double> defaults = {
    {"Mesh.ElementOrder", 1.0},
    {"Mesh.MshFileVersion", 2.2},
    {"Mesh.Binary", 0.0},
    {"Mesh.SaveAll", 0.0},
    {"Mesh.Algorithm", 6.0}, // Frontal-Delaunay for 2D
    {"Mesh.MeshSizeMin", 5.0e-4},
    {"Mesh.MeshSizeMax", 5.0e-3},
    // Match mesh_gmsh first-shot defaults.
    {"Mesh.MeshSizeFromPoints", 1.0},
    {"Mesh.MeshSizeFromCurvature", 1.0},
    {"Mesh.MeshSizeExtendFromBoundary", 1.0},
    {"Mesh.MinimumElementsPerTwoPi", 48.0},
    {"Mesh.Smoothing", 5.0},
    {"Mesh.Optimize", 1.0},
    {"Mesh.OptimizeNetgen", 1.0},
  };

  for(const auto &[name, value] : defaults) {
    gmsh::option::setNumber(name, value);
  }
  for(const auto &[name, value] : options.optionNumbers) {
    gmsh::option::setNumber(name, value);
  }
  for(const auto &[name, value] : options.optionStrings) {
    gmsh::option::setString(name, value);
  }
}

static void setOptionNumberBestEffort(const std::string &name, double value) {
  try {
    gmsh::option::setNumber(name, value);
  } catch(const std::exception &e) {
    debugLog(
      "[option warning] could not set ",
      name,
      "=",
      value,
      " (",
      e.what(),
      ")\n");
  } catch(...) {
    debugLog(
      "[option warning] could not set ",
      name,
      "=",
      value,
      " (unknown exception)\n");
  }
}

static void applyFragmentRecoveryOptions(const int level) {
  if(level <= 0) return;

  // Recovery profile from mild to aggressive while preserving source-entity
  // provenance (no OCC healShapes here, which may retag entities).
  setOptionNumberBestEffort("Geometry.OCCFixSmallEdges", 1.0);
  setOptionNumberBestEffort("Geometry.OCCFixSmallFaces", 1.0);
  setOptionNumberBestEffort("Geometry.OCCFixDegenerated", 1.0);
  setOptionNumberBestEffort("Geometry.OCCParallel", 0.0);

  if(level >= 2) {
    setOptionNumberBestEffort("Geometry.OCCSewFaces", 1.0);
    setOptionNumberBestEffort("Geometry.Tolerance", 1e-8);
    setOptionNumberBestEffort("Geometry.ToleranceBoolean", 1e-8);
  }
}

static void configureBackgroundSizeField(const BuildResult &result,
                                         int meshDim,
                                         const geometry::MeshFieldSettings &field) {
  if(!field.enabled) return;
  if(field.sampling < 2) {
    throw std::runtime_error("Mesh field sampling must be >= 2.");
  }
  if(!(field.lcMin > 0.0 && field.lcMax > 0.0 && field.lcMin <= field.lcMax)) {
    throw std::runtime_error("Mesh field requires 0 < lcMin <= lcMax.");
  }
  if(!(field.distMin >= 0.0 && field.distMax >= field.distMin)) {
    throw std::runtime_error("Mesh field requires 0 <= distMin <= distMax.");
  }
  debugLog(
    "[mesh field] enabled=true dim=",
    meshDim,
    " lcMin=",
    field.lcMin,
    " lcMax=",
    field.lcMax,
    " distMin=",
    field.distMin,
    " distMax=",
    field.distMax,
    " sampling=",
    field.sampling,
    "\n");

  int distanceField = gmsh::model::mesh::field::add("Distance");
  gmsh::model::mesh::field::setNumber(
    distanceField, "Sampling", static_cast<double>(field.sampling));

  if(meshDim == 2) {
    std::vector<int> curves = result.boundaryCurves;
    if(curves.empty()) {
      std::vector<std::pair<int, int>> entities;
      gmsh::model::getEntities(entities, 1);
      curves.reserve(entities.size());
      for(const auto &[dim, tag] : entities) {
        if(dim == 1) curves.push_back(tag);
      }
    }
    if(curves.empty()) {
      throw std::runtime_error(
        "Field sizing is enabled, but no 1D curves were found for Distance field.");
    }
    gmsh::model::mesh::field::setNumbers(distanceField, "CurvesList", toDoubleList(curves));
  } else if(meshDim == 3) {
    std::vector<int> surfaces = result.finalFaces;
    if(surfaces.empty()) {
      std::vector<std::pair<int, int>> entities;
      gmsh::model::getEntities(entities, 2);
      surfaces.reserve(entities.size());
      for(const auto &[dim, tag] : entities) {
        if(dim == 2) surfaces.push_back(tag);
      }
    }
    if(surfaces.empty()) {
      throw std::runtime_error(
        "Field sizing is enabled, but no 2D surfaces were found for Distance field.");
    }
    gmsh::model::mesh::field::setNumbers(distanceField, "SurfacesList", toDoubleList(surfaces));
  } else {
    throw std::runtime_error("Unsupported mesh dimension for field sizing: " + std::to_string(meshDim));
  }

  const int thresholdField = gmsh::model::mesh::field::add("Threshold");
  gmsh::model::mesh::field::setNumber(thresholdField, "InField", static_cast<double>(distanceField));
  gmsh::model::mesh::field::setNumber(thresholdField, "LcMin", field.lcMin);
  gmsh::model::mesh::field::setNumber(thresholdField, "LcMax", field.lcMax);
  gmsh::model::mesh::field::setNumber(thresholdField, "DistMin", field.distMin);
  gmsh::model::mesh::field::setNumber(thresholdField, "DistMax", field.distMax);
  gmsh::model::mesh::field::setNumber(thresholdField, "StopAtDistMax", field.stopAtDistMax ? 1.0 : 0.0);

  gmsh::model::mesh::field::setAsBackgroundMesh(thresholdField);
}

static std::vector<std::string> collectPhysicalGroupNames(const BuildResult &result) {
  std::vector<std::string> names;
  names.reserve(result.physicalGroups.size());
  for(const auto &[name, rec] : result.physicalGroups) {
    (void)rec;
    names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  names.erase(std::unique(names.begin(), names.end()), names.end());
  return names;
}

class GmshSessionGuard {
public:
  explicit GmshSessionGuard(const geometry::MeshingOptions &options) {
    gmsh::initialize();
    active_ = true;

    const double terminalOutput =
      (options.debug || !options.suppressGmshTerminalOutput) ? 1.0 : 0.0;
    gmsh::option::setNumber("General.Terminal", terminalOutput);
    gmsh::option::setNumber("General.Verbosity", options.debug ? 4.0 : 0.0);
    gmsh::option::setNumber("General.AbortOnError", 2.0);
  }

  GmshSessionGuard(const GmshSessionGuard &) = delete;
  GmshSessionGuard &operator=(const GmshSessionGuard &) = delete;

  ~GmshSessionGuard() {
    if(!active_) return;
    try {
      gmsh::finalize();
    } catch(...) {
      // no-throw destructor
    }
  }

private:
  bool active_ = false;
};

} // anonymous namespace

namespace geometry {

MeshingResult make_mesh(
  const std::filesystem::path &geometrySourcePath,
  const std::filesystem::path &meshOutputPath,
  double liquidLevel,
  const MeshingOptions &options) {

  const std::filesystem::path inputPath = std::filesystem::absolute(geometrySourcePath);
  if(!std::filesystem::exists(inputPath)) {
    throw std::runtime_error(
      "geometry source does not exist: " + inputPath.string());
  }
  const GeometrySourceFormat sourceFormat = inferGeometrySourceFormat(inputPath);

  gDebugLogEnabled = options.debug;
  GmshSessionGuard gmshSession(options);
  bool loggerStarted = false;
  try {
    gmsh::logger::start();
    loggerStarted = true;
  } catch(...) {
    loggerStarted = false;
  }

  auto stopLogger = [&]() {
    if(!loggerStarted) return;
    try {
      gmsh::logger::stop();
    } catch(...) {
      // best-effort
    }
    loggerStarted = false;
  };

  auto collectLoggerLines = [&]() {
    std::vector<std::string> lines;
    if(!loggerStarted) return lines;
    try {
      gmsh::logger::get(lines);
    } catch(...) {
      lines.clear();
    }
    return lines;
  };

  auto appendLoggerSnippet = [](std::ostringstream &oss,
                                const std::vector<std::string> &lines) {
    if(lines.empty()) return;
    std::vector<std::string> tagged;
    tagged.reserve(lines.size());
    for(const auto &line : lines) {
      if(line.find("Error") != std::string::npos ||
         line.find("Warning") != std::string::npos) {
        tagged.push_back(line);
      }
    }
    const std::vector<std::string> &src = tagged.empty() ? lines : tagged;
    const std::size_t keep = std::min<std::size_t>(src.size(), 8);
    oss << " | gmsh_log_tail=[";
    for(std::size_t i = src.size() - keep; i < src.size(); ++i) {
      if(i != src.size() - keep) oss << " ; ";
      oss << src[i];
    }
    oss << "]";
  };

  std::string stage = "initialize";
  try {
    stage = std::string("load_") + geometrySourceFormatName(sourceFormat);
    const std::string inputGeometrySource = inputPath.string();
    LoadedGeometry loaded =
      (sourceFormat == GeometrySourceFormat::LegacyJson)
        ? loadGeometryFromLegacyJsonSource(inputPath)
        : loadGeometryFromDxfSource(inputPath);
    std::vector<Component> activeComponents = loaded.components;
    if(activeComponents.empty()) {
      throw std::runtime_error("geometry source did not produce any geometry");
    }

    const bool liquidFromArgument = std::isfinite(liquidLevel);
    const double effectiveLiquidLevel = liquidFromArgument
      ? liquidLevel
      : loaded.liquidLevel.value_or(0.004);
    const std::string liquidLevelSource = liquidFromArgument
      ? "argument"
      : (loaded.liquidLevel ? geometrySourceFormatName(sourceFormat) : "default");

    stage = "apply_liquid_level";
    int shrinkCandidates = 0;
    int shrinkUpdated = 0;
    for(auto &component : activeComponents) {
      applyLiquidLevelToShrinkageRecursive(
        component,
        effectiveLiquidLevel,
        shrinkCandidates,
        shrinkUpdated);
    }
    debugLog(
      "[liquid level] value=",
      effectiveLiquidLevel,
      " source=",
      liquidLevelSource,
      " shrinkCandidates=",
      shrinkCandidates,
      " shrinkUpdated=",
      shrinkUpdated,
      "\n");

    stage = "build_geometry";
    BuildResult buildResult;
    auto runBuildAttempt = [&](const std::vector<Component> &attemptRoots,
                               const std::string &attemptLabel,
                               const int fragmentRecoveryLevel,
                               BuildResult &outResult,
                               std::string &outError) -> bool {
      outError.clear();
      const std::string modelName = inputPath.filename().string() +
                                    (attemptLabel.empty() ? std::string("") : "_" + attemptLabel);
      gmsh::model::add(modelName);
      applyMeshingOptions(options);
      applyFragmentRecoveryOptions(fragmentRecoveryLevel);
      try {
        Builder builder;
        outResult = builder.build(attemptRoots);
        validateBuildResult(outResult);
        return true;
      } catch(const std::exception &e) {
        outError = e.what();
      } catch(...) {
        outError = "unknown non-std exception";
      }
      return false;
    };

    std::string buildError;
    // Run fragment build once with aggressive OCC recovery options enabled
    // up front; do not execute staged retry loops.
    if(!runBuildAttempt(activeComponents, "initial", 2, buildResult, buildError)) {
      if(isFragmentBuilderFailureMessage(buildError)) {
        throw std::runtime_error(
          buildError +
          " (fragment failed with aggressive OCC recovery options enabled)");
      }
      throw std::runtime_error(buildError);
    }

    const std::vector<std::string> rootNames = collectRootNames(activeComponents);

    if(options.dimension != 2 && options.dimension != 3) {
      throw std::runtime_error("MeshingOptions.dimension must be 2 or 3.");
    }

    stage = "configure_size_field";
    configureBackgroundSizeField(buildResult, options.dimension, options.field);

    stage = "mesh_generate";
    gmsh::model::mesh::generate(options.dimension);
    gmsh::model::mesh::removeDuplicateNodes();
    gmsh::model::mesh::removeDuplicateElements();

    stage = "validate_live_mesh";
    if(options.dimension == 2) {
      fixSurfaceMeshOrientation();
      if(checkTriangleOrientation()) {
        throw std::runtime_error(
          "triangle orientation is inconsistent after attempted correction");
      }
      if(countTriangleElements() == 0) {
        throw std::runtime_error("generated mesh contains no triangle elements");
      }
    } else {
      if(countElementsInDimension(3) == 0) {
        throw std::runtime_error("generated mesh contains no 3D volume elements");
      }
    }

    stage = "write_mesh";
    std::filesystem::path meshPath = std::filesystem::absolute(meshOutputPath);
    if(meshPath.extension() != ".msh") {
      meshPath += ".msh";
    }
    gmsh::write(meshPath.string());

    std::optional<std::filesystem::path> geometryConfigPath;
    if(options.writeGeometryConfig) {
      stage = "write_geometry_config";
      std::filesystem::path configStem = meshPath;
      configStem.replace_extension("");
      const std::filesystem::path cfgPath =
        configStem.string() + "_geometry_config.yaml";
      writeGeometryAutogenConfig(
        cfgPath.string(),
        inputGeometrySource,
        geometrySourceFormatName(sourceFormat),
        meshPath.string(),
        rootNames,
        std::optional<double>{effectiveLiquidLevel},
        liquidLevelSource,
        buildResult,
        options);
      geometryConfigPath = cfgPath;
    }

    stage = "validate_written_mesh";
    gmsh::clear();
    gmsh::open(meshPath.string());

    if(options.dimension == 2) {
      if(checkNonManifoldTriangleEdges()) {
        throw std::runtime_error("non-manifold triangle edges detected");
      }
      if(identifyFaulty1DSegments()) {
        throw std::runtime_error("faulty tagged 1D segments detected");
      }
      if(checkTriangleOrientation()) {
        throw std::runtime_error("triangle orientation check failed on written mesh");
      }
      if(countTriangleElements() == 0) {
        throw std::runtime_error("written mesh contains no triangle elements");
      }
    } else {
      if(countElementsInDimension(3) == 0) {
        throw std::runtime_error("written mesh contains no 3D volume elements");
      }
    }

    MeshingResult out;
    out.meshPath = meshPath;
    out.geometryConfigPath = geometryConfigPath;
    out.physicalGroupNames = collectPhysicalGroupNames(buildResult);
    stopLogger();
    return out;
  } catch(const std::exception &e) {
    const std::vector<std::string> lines = collectLoggerLines();
    std::ostringstream oss;
    oss << "make_mesh failed at stage '" << stage << "'";
    const std::string reason = e.what();
    if(!reason.empty()) {
      oss << ": " << reason;
    } else {
      oss << ": unknown exception";
    }
    appendLoggerSnippet(oss, lines);
    stopLogger();
    throw std::runtime_error(oss.str());
  } catch(...) {
    const std::vector<std::string> lines = collectLoggerLines();
    std::ostringstream oss;
    oss << "make_mesh failed at stage '" << stage
        << "': unknown non-std exception";
    appendLoggerSnippet(oss, lines);
    stopLogger();
    throw std::runtime_error(oss.str());
  }
}

} // namespace geometry
