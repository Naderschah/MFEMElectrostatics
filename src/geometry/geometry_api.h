#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace geometry {

struct MeshFieldSettings {
  // Enable Distance+Threshold background field sizing.
  bool enabled = true;
  // Number of sampling points used by the Distance field.
  int sampling = 120;
  // Target size close to tagged boundaries.
  double lcMin = 5.0e-4;
  // Target size away from tagged boundaries.
  double lcMax = 5.0e-3;
  // Distance where lcMin starts transitioning.
  double distMin = 0.0;
  // Distance where lcMax is reached.
  double distMax = 0.08;
  // If true, keep lcMax beyond distMax.
  bool stopAtDistMax = true;
};

struct MeshingOptions {
  bool debug = false;
  bool suppressGmshTerminalOutput = true;
  bool writeGeometryConfig = true;
  bool fieldCageNetworkFromGeometry = true;
  // Mesh topological dimension. Existing geometry pipeline currently emits 2D.
  int dimension = 2;
  // Raw gmsh option overrides applied before meshing.
  std::map<std::string, double> optionNumbers;
  std::map<std::string, std::string> optionStrings;
  MeshFieldSettings field;
};

struct MeshingResult {
  std::filesystem::path meshPath;
  std::optional<std::filesystem::path> geometryConfigPath;
  std::vector<std::string> physicalGroupNames;
};

MeshingResult make_mesh(
  const std::filesystem::path &geometrySourcePath,
  const std::filesystem::path &meshOutputPath,
  double liquidLevel,
  const MeshingOptions &options = {});

} // namespace geometry
