#pragma once
#include <string>
#include <vector>
#include <utility>   // std::pair

/*
To abstract the objects to be subtracted from the background volume 
-> To make life a little easier in constructing the geometries
*/


// Gmsh API
#include <gmsh.h>

namespace tpc::geom {

using DimTag = std::pair<int,int>;

// Anything you want to partition out of the background volume.
struct Tool {
  std::string name;  // e.g. "Anode", "Cathode", "PTFE"
  int tag;           // volume tag
  int surfBC = -1;   // Physical Surface id (optional; -1 = none)
  int volBC  = -1;   // Physical Volume id  (optional; -1 = none)
};

// Register a tool you’ve just built (one-liner at call site)
void registerTool(std::vector<Tool> &tools,
                  const std::string &name, int volTag,
                  int surfBC, int volBC,
                  std::vector<Tool> *fluids,
                  std::vector<Tool> *cutter);

void fragmentAllTogether(int &background_vol, std::vector<Tool> &tools);

// Fragment background with all tools in one pass, update tags via provenance
void fragmentBackgroundWithTools(int &background_vol,
                                 std::vector<Tool> &tools);
// Fragments with tools and objects again due to differing priority on electrodes and volumes
// All objects are updated here due to retagging
void carveToolsByCutters(std::vector<Tool> &fluids,
                         std::vector<Tool> &cutters,
                         std::vector<Tool> &allTools);
                         
// Return the set of (unique) surface tags bounding a volume
std::vector<int> getSurfaces(int volTag);

// Create Physical groups (surfaces/volumes) for each tool, if requested
void tagPhysicals(const std::vector<Tool> &tools);

void printRegisteredTools(const std::vector<Tool> &tools,
                          const std::string &title = "Registered tools");


} // namespace tpc::geom
