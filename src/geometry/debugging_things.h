#pragma once
#include <string>
#include <vector>
#include <unordered_set>
#include <utility>   // std::pair

// Gmsh API
#include <gmsh.h>

namespace tpc::dbg {

using DimTag = std::pair<int,int>;

// Print any 2D surfaces whose oriented 1D boundary does not form a closed loop
void reportOpenSurfaceLoops();

// Return the unique set of surface tags that bound a 3D volume
std::unordered_set<int> surfaceSetOfVolume(int volTag);

// Human-readable description of a surface: bbox, type, parent volumes, membership
void describeSurface(int surfTag,
                     const std::unordered_set<int> &bgSurfs,
                     const std::unordered_set<int> &anodeSurfs,
                     const std::unordered_set<int> &cathodeSurfs);

// Convenience: summarize one surface and which volumes/sets it belongs to
void debugSurfaceTag(int surfTag, int background_vol, int anodeVol, int cathodeVol);

// Concise debug wrapper for occ::fragment: logs inputs/outputs and runs the op
void debugFragment(const std::string &title,
                   const std::vector<DimTag> &objects,
                   const std::vector<DimTag> &tools,
                   std::vector<DimTag> &newEntities,
                   std::vector<std::vector<DimTag>> &provenanceByInput,
                   int tag = -1,
                   bool removeObject = true,
                   bool removeTool = false);

// Get all surface tags that bound a given volume (dim=2 tags only)
std::vector<int> getSurfaces(int volTag);

// Remove any 2D faces that are not on the boundary of any 3D volume
void removeOrphanSurfaces();

} // namespace tpc::dbg
