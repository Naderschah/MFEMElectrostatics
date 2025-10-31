#include "debugging_things.h"

#include <iostream>
#include <sstream>
#include <unordered_map>

namespace tpc::dbg {

static std::string joinTags(const std::vector<DimTag> &vec) {
  std::ostringstream ss;
  for (size_t i = 0; i < vec.size(); ++i) {
    if (i > 0) ss << ", ";
    ss << vec[i].second;
  }
  return ss.str();
}

void reportOpenSurfaceLoops() {
  std::vector<DimTag> surfs;
  gmsh::model::getEntities(surfs, 2);

  int bad = 0;
  for (const auto &s : surfs) {
    // curves on this surface
    std::vector<DimTag> curves;
    gmsh::model::getBoundary({s}, curves,
                             /*combined=*/false,
                             /*oriented=*/true,
                             /*recursive=*/false);

    // count endpoints of all curves
    std::unordered_map<int,int> pointDegree;

    for (const auto &c : curves) {
      if (c.first != 1) continue;
      std::vector<DimTag> pts;
      gmsh::model::getBoundary({c}, pts,
                               /*combined=*/false,
                               /*oriented=*/true,
                               /*recursive=*/false);
      for (const auto &p : pts)
        if (p.first == 0) pointDegree[p.second]++; // end points
    }

    // endpoints in a closed loop come in pairs (even degree)
    bool open = false;
    for (const auto &kv : pointDegree) {
      if (kv.second % 2 != 0) { open = true; break; }
    }
    if (open) {
      bad++;
      std::cout << "[OPEN] surface tag " << s.second
                << " with " << curves.size()
                << " boundary curves (odd-degree point found)\n";
    }
  }

  if (bad == 0) std::cout << "All surface loops appear closed.\n\n\n\n";
}

std::unordered_set<int> surfaceSetOfVolume(int volTag) {
  std::vector<DimTag> bdr, arg = {{3, volTag}};
  gmsh::model::getBoundary(arg, bdr,
                           /*combined=*/true,
                           /*oriented=*/false,
                           /*recursive=*/false);
  std::unordered_set<int> s;
  for (auto &dt : bdr) if (dt.first == 2) s.insert(dt.second);
  return s;
}

void describeSurface(int surfTag,
                     const std::unordered_set<int> &bgSurfs,
                     const std::unordered_set<int> &anodeSurfs,
                     const std::unordered_set<int> &cathodeSurfs)
{
  // bbox
  double xmin, ymin, zmin, xmax, ymax, zmax;
  gmsh::model::getBoundingBox(2, surfTag, xmin, ymin, zmin, xmax, ymax, zmax);
  const double cx = 0.5 * (xmin + xmax);
  const double cy = 0.5 * (ymin + ymax);
  const double cz = 0.5 * (zmin + zmax);

  // type (output parameter)
  std::string typ;
  gmsh::model::getType(2, surfTag, typ); // e.g. "Plane Surface", "Cylinder"

  // parents (volumes that own this surface)
  std::vector<int> upDims, upTags;
  gmsh::model::getAdjacencies(2, surfTag, upDims, upTags);
  std::vector<int> parents;
  for (size_t i = 0; i < upDims.size(); ++i)
    if (upDims[i] == 3) parents.push_back(upTags[i]);

  std::cout << "Surface " << surfTag
            << "  type=" << typ
            << "  bbox=[(" << xmin << "," << ymin << "," << zmin << ")→("
            << xmax << "," << ymax << "," << zmax << ")]"
            << "  center=(" << cx << "," << cy << "," << cz << ")\n";

  std::cout << "  Belongs to volumes: ";
  for (int v : parents) std::cout << v << " ";
  std::cout << "\n";

  std::cout << "  In sets: "
            << (bgSurfs.count(surfTag) ? "Background " : "")
            << (anodeSurfs.count(surfTag) ? "Anode " : "")
            << (cathodeSurfs.count(surfTag) ? "Cathode " : "")
            << "\n";
}

void debugSurfaceTag(int surfTag, int background_vol, int anodeVol, int cathodeVol) {
  auto bgSet      = surfaceSetOfVolume(background_vol);
  auto anodeSet   = surfaceSetOfVolume(anodeVol);
  auto cathodeSet = surfaceSetOfVolume(cathodeVol);
  describeSurface(surfTag, bgSet, anodeSet, cathodeSet);
}

void debugFragment(const std::string &title,
                   const std::vector<DimTag> &objects,
                   const std::vector<DimTag> &tools,
                   std::vector<DimTag> &newEntities,
                   std::vector<std::vector<DimTag>> &provenanceByInput,
                   int tag,
                   bool removeObject,
                   bool removeTool)
{
  std::cout << "\n=== " << title << " ===\n";
  std::cout << "Objects (" << objects.size() << "): " << joinTags(objects);
  if (removeObject) std::cout << "  [marked for removal]";
  std::cout << '\n';

  std::cout << "Tools (" << tools.size() << "): " << joinTags(tools);
  if (removeTool) std::cout << "  [marked for removal]";
  std::cout << '\n';

  gmsh::model::occ::fragment(objects, tools,
                             newEntities, provenanceByInput,
                             tag, removeObject, removeTool);

  std::cout << "NewEntities (" << newEntities.size() << "): "
            << joinTags(newEntities) << '\n';

  for (size_t i = 0; i < provenanceByInput.size(); ++i)
    std::cout << "Provenance[" << i << "] ("
              << provenanceByInput[i].size() << "): "
              << joinTags(provenanceByInput[i]) << '\n';

  std::cout << "============================================================\n";
}

std::vector<int> getSurfaces(int volTag) {
  std::vector<DimTag> vol = {{3, volTag}};
  std::vector<DimTag> surfs;
  gmsh::model::getBoundary(vol, surfs,
                           /*combined=*/true,
                           /*oriented=*/false,
                           /*recursive=*/false);

  std::vector<int> surfTags;
  surfTags.reserve(surfs.size());
  for (auto &dt : surfs)
    if (dt.first == 2) surfTags.push_back(dt.second);
  return surfTags;
}

void removeOrphanSurfaces() {
  // Collect all 2D faces that actually bound some 3D volume
  std::vector<DimTag> vols;
  gmsh::model::getEntities(vols, 3);

  std::unordered_set<int> boundarySurfs;
  for (const auto &v : vols) {
    std::vector<DimTag> bdr;
    gmsh::model::getBoundary({v}, bdr,
                             /*combined=*/true,
                             /*oriented=*/false,
                             /*recursive=*/false);
    for (const auto &dt : bdr) if (dt.first == 2) boundarySurfs.insert(dt.second);
  }

  // Find *all* 2D faces
  std::vector<DimTag> surfs;
  gmsh::model::getEntities(surfs, 2);

  // Any surface not in boundarySurfs is orphaned → remove it
  std::vector<DimTag> toRemove;
  toRemove.reserve(surfs.size());
  for (const auto &s : surfs) {
    if (!boundarySurfs.count(s.second)) toRemove.push_back(s);
  }

  if (!toRemove.empty()) {
    std::cout << "Removing orphan surfaces: ";
    for (auto &s : toRemove) std::cout << s.second << " ";
    std::cout << "\n";
    gmsh::model::occ::remove(toRemove, /*recursive=*/true);
  }
}

} // namespace tpc::dbg
