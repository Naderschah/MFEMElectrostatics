#include <gmsh.h>
#include <cmath>
#include <vector>
#include <utility>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <unordered_set>


#include "geometry_constants.h" 
#include "partition_tools.h" 
#include "debugging_things.h"
using namespace tpc::dbg;

//---------------------------- Electrodes --------------------------------

auto makeWire = [](double xc, double yc, double zc,
                   double length, double radius)
{
  return gmsh::model::occ::addCylinder(
      xc - length * 0.5, yc, zc,
      length, 0.0, 0.0,  // oriented along X
      radius, -1, 2.0 * M_PI);
};

// Create all wire cylinders for one electrode (anode or cathode).
// Returns vector of volume tags.
static std::vector<int> makeElectrodeWires(int n_wires,
                                           double wire_diameter,
                                           double inner_radius,
                                           double zBase)
{
  std::vector<int> wireTags;
  if (n_wires <= 0) return wireTags;

  const double rWire = wire_diameter * 0.5;
  const double R     = inner_radius;

  // Feasibility: need room for n circles of radius r with uniform surface gaps
  // c = (2R + 2r) / (n + 1) must be > 2r  =>  R > n*r
  if (R <= n_wires * rWire) {
    // Not enough room to place n wires with equal end/neighbor gaps
    return wireTags;
  }

  // Center-to-center spacing that yields equal end gap and inter-wire surface gap
  const double c = (2.0 * R + 2.0 * rWire) / (n_wires + 1.0);

  // Symmetric center positions with spacing c
  std::vector<double> yPositions;
  yPositions.reserve(n_wires);
  const double y0 = -0.5 * (n_wires - 1) * c;
  for (int i = 0; i < n_wires; ++i) yPositions.push_back(y0 + i * c);

  // Helper to make a cylinder along X
  auto makeWire = [](double xc, double yc, double zc, double length, double radius) {
    return gmsh::model::occ::addCylinder(xc - length * 0.5, yc, zc,
                                         length, 0.0, 0.0, radius,
                                         /*tag=*/-1);
  };

  // Compute chord length so each wire is fully contained inside the inner radius
  for (double y : yPositions) {
    // Safety: by construction |y| <= R - (c - 2r) - something; but keep guard
    double delta = std::max(0.0, std::abs(y) - rWire);
    double chord = 2.0 * std::sqrt(std::max(0.0, R * R - delta * delta));
    if (chord > 0.0) {
      int tag = makeWire(0.0, y, zBase, chord, rWire);
      wireTags.push_back(tag);
    }
  }

  return wireTags;
}

static int makeRingElectrode(double xCenter,
                             double yCenter,
                             double zBase,
                             double thickness,
                             double innerR,
                             double outerR)
{
  if(outerR <= innerR || innerR <= 0.0 || thickness <= 0.0)
    throw std::runtime_error("makeRingElectrode: invalid parameters");

  // Build outer & inner solid cylinders
  const int outerTag = gmsh::model::occ::addCylinder(xCenter, yCenter, zBase,
                                                     0.0, 0.0, thickness,
                                                     outerR, -1);
  const int innerTag = gmsh::model::occ::addCylinder(xCenter, yCenter, zBase,
                                                     0.0, 0.0, thickness,
                                                     innerR, -1);
  // Subtract inner from outer to make a ring (hollow cylinder)
  std::vector<DimTag> result;
  std::vector<std::vector<DimTag>> outMap; // unused, but required by overload
  gmsh::model::occ::cut({{3, outerTag}}, {{3, innerTag}}, result, outMap, /*tag*/ -1, /*removeObject=*/true, /*removeTool=*/true);

  if(result.empty())
    throw std::runtime_error("makeRingElectrode: OCC cut produced no volume");

  // Usually a single volume; if multiple, return the first (caller may inspect others if desired)
  return result.front().second;
}
// Create a full electrode: ring + all its wires merged into one volume.
// Returns the resulting merged (unioned) volume tag.
static int makeParallelWireElectrodeAssembly(double xCenter,
                         double yCenter,
                         double zBase,
                         double zBaseWires,
                         double ring_thickness,
                         double inner_radius,
                         double outer_radius,
                         int n_wires,
                         double wire_diameter)
{
  // --- Create ring shell ---
  int ringTag = makeRingElectrode(xCenter, yCenter, zBase,
                                  ring_thickness, inner_radius, outer_radius);

  // --- Create parallel wire grid ---
  std::vector<int> wireTags = makeElectrodeWires(
      n_wires, wire_diameter, inner_radius, zBaseWires);

  if (wireTags.empty()) return ringTag;

  // --- Fuse ring + wires ---
  using DimTag = std::pair<int,int>;
  std::vector<DimTag> out;
  std::vector<std::vector<DimTag>> map;
  std::vector<DimTag> wires;
  for (int t : wireTags) wires.emplace_back(3, t);

  gmsh::model::occ::fuse({{3, ringTag}}, wires, out, map,
                         /*tag=*/-1, /*removeObject=*/true, /*removeTool=*/true);

  if (out.empty())
    throw std::runtime_error("makeElectrode: fuse produced no volume");

  return out.front().second;
}

// --------------- PTFE Wall ----------------------

static int makeCylindricalSleeve(double zMin, double zMax,
                                 double rInner, double rOuter)
{
  const double h = zMax - zMin;
  if (h <= 0.0 || rOuter <= rInner || rInner <= 0.0) return -1;

  const int outerTag = gmsh::model::occ::addCylinder(0.0, 0.0, zMin,
                                                     0.0, 0.0, h,
                                                     rOuter, /*tag*/-1);
  const int innerTag = gmsh::model::occ::addCylinder(0.0, 0.0, zMin,
                                                     0.0, 0.0, h,
                                                     rInner, /*tag*/-1);

  std::vector<std::pair<int,int>> res;
  std::vector<std::vector<std::pair<int,int>>> map;
  gmsh::model::occ::cut({{3, outerTag}}, {{3, innerTag}},
                        res, map, /*tag*/-1, /*removeObject=*/true, /*removeTool=*/true);

  if (res.empty()) return -1;
  return res.front().second;
}

// -------------------------- Main ------------------------------

int main() {
  gmsh::initialize();
  gmsh::model::add("tpc_occ");
  // ---- Constants ----

  const double H     = DriftRegionHeight + 2 * ring_thickness;
  const double R     = outer_radius * 1.1;
  const double rWire = wire_diameter * 0.5;

  // TODO Precompute all possible physical alignments and verify them here before moving on
  // TODO: Need to homogenise how i build things, I think i got my methods confused at this point

  // ---------------------  Build Geometry ---------------------
  // Outer Volume
  int background_vol = gmsh::model::occ::addCylinder(
      0, 0, 0,
      0, 0, H,
      R
  );

  // Collect all parts to subtract from background
  std::vector<tpc::geom::Tool> tools;        // Stores all objects 
  std::vector<tpc::geom::Tool> fluids;       // LXe / GXe only (low priority cutters relative to background)
  std::vector<tpc::geom::Tool> cutters;      // electrodes+PTFE (High priority cutter)


  // --- Anode (wires at bottom extent: z_min + rWire) ---
  if (USE_ANODE) {
    const double anode_zBase       = anode_wire_height - ring_thickness;
    const double anode_wireCenterZ = anode_zBase + rWire + 0.005; // tiny nudge up
    const int anode = makeParallelWireElectrodeAssembly(
        0.0, 0.0, anode_zBase, anode_wireCenterZ,
        ring_thickness, inner_radius, outer_radius,
        n_wires_anode, wire_diameter
    );
    tpc::geom::registerTool(tools, "Anode", anode, /*surfBC=*/anode_BC_index, /*volBC=*/-1, &fluids, &cutters);
  }
  if (USE_GATE) {
  const double gate_zBase       = gate_wire_height;                           // ring bottom
  const double gate_wireCenterZ = gate_zBase + ring_thickness - rWire - 0.005; // tiny nudge down
  const int gate = makeParallelWireElectrodeAssembly(
      0.0, 0.0, gate_zBase, gate_wireCenterZ,
      ring_thickness, inner_radius, outer_radius,
      n_wires_gate, wire_diameter
  );

  tpc::geom::registerTool(tools, "Gate", gate, /*surfBC=*/gate_BC_index, /*volBC=*/-1, &fluids, &cutters);
  }
  // --- Cathode (wires at top extent: z_max - rWire) ---
  if (USE_CATHODE) {
    const double cathode_zBase       = cathode_wire_height;
    const double cathode_wireCenterZ = cathode_zBase + ring_thickness - rWire - 0.005; // tiny nudge down
    const int cathode = makeParallelWireElectrodeAssembly(
        0.0, 0.0, cathode_zBase, cathode_wireCenterZ,
        ring_thickness, inner_radius, outer_radius,
        n_wires_cathode, wire_diameter
    );
    tpc::geom::registerTool(tools, "Cathode", cathode, /*surfBC=*/cathode_BC_index, /*volBC=*/-1, &fluids, &cutters);
  }

  if (USE_PTFE)
  {
  // We will create two sleeves on these *normalized* intervals:
  auto addPTFE = [&](const char* name, double z0, double z1) {
    const double zMin = std::min(z0, z1);
    const double zMax = std::max(z0, z1);
    if (zMax - zMin <= 1e-9) return; // degenerate span → skip
    const int vol = makeCylindricalSleeve(zMin, zMax, /*rInner=*/inner_radius, /*rOuter=*/outer_radius);
    if (vol >= 0) {
      // No surface BC; tag as PTFE material volume
      tpc::geom::registerTool(tools, name, vol, /*surfBC=*/-1, /*volBC=*/PTFE_Volume_index, &fluids, &cutters);
    } else {
      std::cout << "[warn] PTFE \"" << name << "\" not created (empty span or OCC cut failed)\n";
    }
  };
  // Gate to anode // TODO WHY ARE THESE THE BOUNDS THIS MAKES NO SENSE
  addPTFE("PTFE_t", gate_wire_height + ring_thickness, anode_wire_height - ring_thickness);
  // Cathode to Gate
  addPTFE("PTFE_b", cathode_wire_height + ring_thickness, gate_wire_height);
  }
  auto addMedium = [&](const char* name, double z0, double z1, int volBC) {
    const double zMin = std::min(z0, z1), zMax = std::max(z0, z1);
    if (zMax - zMin <= 1e-9) return; // TODO Make it err
    int v = gmsh::model::occ::addCylinder(0,0, zMin, 0,0, (zMax - zMin), /*radius=*/inner_radius, /*tag*/-1);
    tpc::geom::registerTool(tools, name, v, /*surfBC=*/-1, /*volBC=*/volBC, &fluids, &cutters);
  };

  // TODO Im not sure if the material gets holes
  if (USE_LXe && !USE_GXe)
  {
    addMedium("LXe", 0.0, anode_wire_height + ring_thickness, LXe_Volume_index);
  }
  else if (!USE_LXe && USE_GXe)
  {
    addMedium("GXe", 0.0, anode_wire_height + ring_thickness, GXe_Volume_index);
  }
  else if (USE_LXe && USE_GXe)
  {
    addMedium("LXe", 0.0, gate_wire_height, LXe_Volume_index);
    addMedium("GXe", gate_wire_height + ring_thickness, anode_wire_height + ring_thickness, GXe_Volume_index);
  }

  tpc::geom::printRegisteredTools(tools, "Tools BEFORE fragment");
  // --- Partition background once with whatever tools we have ---
  if (!tools.empty()) {
    // verbose version for debugging (optional):
    std::vector<DimTag> objs = {{3, background_vol}};
    std::vector<DimTag> tdim;
    tdim.reserve(tools.size());
    for (auto &t : tools) tdim.emplace_back(3, t.tag);

    // TODO FIXME Need to cut electrodes out of LXe -> Cant seem to do it without loosing reference 
    // Cutters didnt work because we lost reference second part worked fine but no liquid xenon
    //tpc::geom::carveToolsByCutters(fluids, cutters, tools);
    tpc::geom::fragmentAllTogether(background_vol, tools);
    // Old Old
    //tpc::geom::fragmentBackgroundWithTools(background_vol, tools);
  } else {
    std::cout << "[info] No tools selected; skipping fragment.\n";
  }

  // ---------------------  Synchronize & cleanup ---------------------
  gmsh::model::occ::removeAllDuplicates();
  tpc::dbg::removeOrphanSurfaces(); // Not required, but might as well TODO Remove when this is more developed
  gmsh::model::occ::synchronize();
  tpc::geom::printRegisteredTools(tools, "Tools AFTER fragment");
  tpc::dbg::reportOpenSurfaceLoops();

  // ------------  Create Physical Groups & Save ---------------------
  gmsh::model::addPhysicalGroup(3, {background_vol}, TPC_Volume_index);
  gmsh::model::setPhysicalName(3, TPC_Volume_index, "TPC_Volume");

  // Per-tool surfaces/volumes (based on surfBC/volBC each tool requested)
  tpc::geom::tagPhysicals(tools);

  // -------------------- Misc ---------------------
  gmsh::option::setNumber("General.Terminal", 1);
  gmsh::option::setNumber("General.Verbosity", 5);

  // Pure CAD (no physicals)
  gmsh::write("tpc_occ.brep");
  // Internal system (includes physicals) -> DOes not work
  // gmsh::write("tpc_occ.geo_unrolled");TODO Compiel with med support

  // -------------------- Meshing ---------------------
  gmsh::option::setNumber("Mesh.SaveAll", 0);
  gmsh::option::setNumber("Mesh.MshFileVersion", 2.2);
  gmsh::option::setNumber("Mesh.Optimize", 1);
  // Multithreaded
  gmsh::option::setNumber("General.NumThreads", threads);

  // lcMax from the smallest present wire plane
  auto lcWireFor = [&](int n_wires) {
    const double r   = rWire;
    const double c   = (2.0 * inner_radius + 2.0 * r) / (n_wires + 1.0);
    const double gap = std::max(0.0, c - 2.0 * r);
    const double lc_circ = 2.0 * M_PI * r / 20.0;  // ~20 points around wire
    const double lc_rad  = r / 3.0;                // ~3 elems across radius
    const double lc_gap  = (gap > 0.0) ? (gap * 0.5) : lc_circ;
    return std::max(1e-6, std::min({lc_circ, lc_rad, lc_gap}));
  };

  double lcWire = 0.05; // fallback coarse
  bool anyWire = false; // TODO WHy did i add this 
  if (USE_ANODE)   { lcWire = lcWireFor(n_wires_anode);   anyWire = true; }
  if (USE_CATHODE) { lcWire = anyWire ? std::min(lcWire, lcWireFor(n_wires_cathode))
                                      : lcWireFor(n_wires_cathode); anyWire = true; }

  if (anyWire) gmsh::option::setNumber("Mesh.CharacteristicLengthMax", lcWire);

  // In case a quick mesh generation is required to check things 
  if (QuickMesh)
  {
    gmsh::option::setNumber("Mesh.CharacteristicLengthMax", lcWire * 3.0);
    gmsh::option::setNumber("Mesh.CharacteristicLengthFromCurvature", 0);
    gmsh::option::setNumber("Mesh.MinimumCirclePoints", 8); // Circle approximated by this many points
    gmsh::option::setNumber("Mesh.Optimize", 0);
  }

  gmsh::model::mesh::generate(3);

  gmsh::write("tpc_occ.msh");
  std::cout << "Created mesh file\n";
  gmsh::finalize();
  return 0;
}
