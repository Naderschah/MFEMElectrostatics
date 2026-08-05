#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace geometry::detail {

enum class GeometrySourceFormat {
  LegacyJson,
  Dxf,
};

struct Vec2 {
  double x = 0.0;
  double y = 0.0;
};

struct CurveSegment {
  enum class Kind { Line, Arc, EllipseArc, BSpline, HyperbolaNurbs };
  Kind kind = Kind::Line;
  Vec2 start, end;
  std::optional<Vec2> center;
  bool orientation = true;
  double a = 0.0, b = 0.0, phiDeg = 0.0;
  bool inversed = false;
  std::vector<Vec2> poles;
  std::vector<double> weights, knots;
  std::vector<int> multiplicities;
  int degree = 3;
  bool periodic = false;
};

struct RepeatSpec {
  int number = 1;
  double dx = 0.0;
  double dy = 0.0;
};

struct Component {
  std::string name;
  std::string material;
  bool hullOnly = false;
  bool markBoundary = false;
  bool isAxisBoundary = false;
  bool isElectrodeBoundary = false;
  bool groupBoundary = false;
  bool splitBoundaryByIndex = false;
  std::string boundaryName;
  bool applyShrinkage = false;
  double shrinkBelowY = 0.0;
  bool hasShrinkBelowY = false;
  double shrinkageFactor = 1.0;
  std::optional<RepeatSpec> repeat;
  std::vector<CurveSegment> outer;
  std::vector<std::vector<CurveSegment>> holes;
  std::vector<Component> children;
};

struct LoadedGeometry {
  std::vector<Component> components;
  std::optional<double> liquidLevel;
  GeometrySourceFormat sourceFormat = GeometrySourceFormat::LegacyJson;
};

GeometrySourceFormat inferGeometrySourceFormat(const std::filesystem::path &path);
const char *geometrySourceFormatName(GeometrySourceFormat format);
LoadedGeometry loadGeometryFromLegacyJsonSource(const std::filesystem::path &path);
LoadedGeometry loadGeometryFromDxfSource(const std::filesystem::path &path);

} // namespace geometry::detail
