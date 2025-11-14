#include <gmsh.h>
#include <cmath>
#include <vector>
#include <utility>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <memory>
#include <filesystem>
#include <cmath>

#include "Config.h"

const double PI = 3.14159265358979323846;

// Builds the outer cryostat cross-section (face, dim=2) and returns its tag.
// D         = cryostat diameter
// liquidLvl = liquid level height above 0
// y0        = vertical position of the straight section bottom
// Rbig      = bottom lower radius (large circle)
// Rsmall    = bottom upper radius (small circle)
// makeFace  = debug param if the loop should be turned to a face or if more will be removed
struct CurveLoop {
  int surfaceTag;              // If a surface was created from this loop
  int tag;                     // gmsh curve loop tag (dim=1 topology)
  std::vector<int> curves;     // ordered curve tags, CCW
};
CurveLoop makeOuterCryostatFace(double D, double liquidLvl, double y0,
                                double Rbig, double Rsmall, bool makeFace)
{
  const double xR   = D / 2.0;
  const double yBot = y0;
  const double yTop = y0 + std::abs(y0) + liquidLvl;

  // Rectangle points & edges
  int pBL = gmsh::model::occ::addPoint(0.0, yBot, 0.0);
  int pBR = gmsh::model::occ::addPoint(xR,  yBot, 0.0);
  int pTR = gmsh::model::occ::addPoint(xR,  yTop, 0.0);
  int eBottom = gmsh::model::occ::addLine(pBL, pBR);   // BL -> BR
  int eRight  = gmsh::model::occ::addLine(pBR, pTR);   // BR -> TR

  // Arc geometry
  const double a1 = -M_PI/2.0;
  const double a2 = a1 + std::asin((D/2.0 - Rsmall) / (Rbig - Rsmall));
  const double cx = 0.0;
  const double cy = y0 + std::sqrt(std::pow(Rbig - Rsmall, 2.0)
                                 - std::pow(D/2.0 - Rsmall, 2.0));

  // Create arc edge (a1 -> a2)
  int eArcTop = gmsh::model::occ::addCircle(cx, cy, 0.0, Rbig, -1, a1, a2);
  gmsh::model::occ::synchronize(); // make arc vertices visible

  // Get the actual OCC endpoints of the arc (0D vertex tags), in order
  gmsh::vectorpair arcEnds;
  gmsh::model::occ::getBoundary({{1, eArcTop}}, arcEnds, /*oriented=*/true, /*recursive=*/false);
  // arcEnds[0] is the "start" vertex of the arc, arcEnds[1] is the "end" vertex
  if (arcEnds.size() != 2 || arcEnds[0].first != 0 || arcEnds[1].first != 0)
    throw std::runtime_error("Failed to get arc endpoints.");

  int pA_start = arcEnds[0].second;  // corresponds to angle a1 (lower)
  int pA_end   = arcEnds[1].second;  // corresponds to angle a2 (upper)
aaaaa
  // Build side lines using the *same* vertex tags to ensure topological continuity
  int eSideHigh = gmsh::model::occ::addLine(pA_end,   pTR); // TR  <- arc end
  int eSideLow  = gmsh::model::occ::addLine(pA_start, pBL); // BL  <- arc start

  // Loop order CCW: BL->BR (bottom), BR->TR (right), TR->arcEnd (sideHigh),
  // arc reversed (arcEnd->arcStart), arcStart->BL (sideLow)
  std::vector<int> edges = { eBottom, eRight, eSideHigh, -eArcTop, eSideLow };
  int loopTag = gmsh::model::occ::addCurveLoop(edges);

  int surf = -1;
  if (makeFace) {
    surf = gmsh::model::occ::addPlaneSurface({ loopTag });
  }
  gmsh::model::occ::synchronize();

  return { surf, loopTag, edges };
}



int main(int argc, char *argv[]) {
  // config path 
  auto cfg = std::make_shared<const Config>(
    Config::Load("../XENONnTSR3/config.yaml")
  );

  gmsh::initialize();
  gmsh::model::add(cfg->geometry_id);

  int dim = 2;
  // Building the cryostat
  // Start with straight segment 
  double CryostatDiameter =                     1460;
  double LiquidLevel =                          4; 
  double CryostatBottomLowerRadius =            1200;
  double CryostatBottomUpperRadius =            220;
  double CryostatStraightVerticalPosition =     -1608.24;
  CurveLoop cryostat = makeOuterCryostatFace(
    /*D*/ CryostatDiameter,
    /*liquidLvl*/ LiquidLevel,
    /*y0*/ CryostatStraightVerticalPosition,
    /*Rbig*/ CryostatBottomLowerRadius,
    /*Rsmall*/ CryostatBottomUpperRadius,
    /*Make face */ true
  );

  gmsh::model::occ::synchronize();
  // -================================ Finish Up ================================
  gmsh::option::setNumber("General.Terminal", 1);
  gmsh::option::setNumber("General.Verbosity", 5);

  gmsh::write(std::filesystem::path(cfg->mesh.path).replace_extension(".brep").string());

  gmsh::option::setNumber("Mesh.SaveAll", 0);
  gmsh::option::setNumber("Mesh.MshFileVersion", 2.2);
  gmsh::option::setNumber("Mesh.Optimize", 1);
  gmsh::option::setNumber("General.NumThreads", cfg->compute.threads.num);


  double characteristicLength = 0.5;
  gmsh::option::setNumber("Mesh.CharacteristicLengthMax", characteristicLength);

  gmsh::model::mesh::generate(2);

  gmsh::write(cfg->mesh.path); 
  std::cout << "Created mesh file\n";
  
  gmsh::finalize();
  return 0; 
}