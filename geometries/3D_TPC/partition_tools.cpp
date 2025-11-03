#include "partition_tools.h"
#include "constants.h"
#include <iostream>
#include <unordered_set>
#include <iomanip>

namespace tpc::geom {


void registerTool(std::vector<Tool> &tools,
                  const std::string &name, int volTag,
                  int surfBC, int volBC,
                  std::vector<Tool> *fluids = nullptr,
                  std::vector<Tool> *cutters = nullptr)
{
  Tool t{name, volTag, surfBC, volBC};
  tools.push_back(t);

  // bucketing LXe and GXe have lower cutting priority
  if (fluids && (volBC == LXe_Volume_index || volBC == GXe_Volume_index))
    fluids->push_back(t);
  else if (cutters)
    cutters->push_back(t);
}


void fragmentAllTogether(int &background_vol, std::vector<Tool> &tools)
{
  // Build OBJECTS: background + all tool volumes (dim=3)
  std::vector<DimTag> objects;
  objects.emplace_back(3, background_vol);
  objects.reserve(1 + tools.size());
  for (auto &t : tools) objects.emplace_back(3, t.tag);

  std::vector<DimTag> newEntities;
  std::vector<std::vector<DimTag>> prov;

  // Mutual fragment: tools set is empty; everything in 'objects' fragments together
  gmsh::model::occ::fragment(objects, /*tools=*/{},
                             newEntities, prov,
                             /*tag=*/-1, /*removeObject=*/true, /*removeTool=*/false);

  // Update background (bucket 0)
  if (!prov.empty() && !prov[0].empty())
    background_vol = prov[0][0].second;

  // Update each tool tag (bucket i+1 corresponds to tools[i])
  for (size_t i = 0; i < tools.size(); ++i)
    if (prov.size() > i + 1 && !prov[i + 1].empty())
      tools[i].tag = prov[i + 1][0].second;
}

void fragmentBackgroundWithTools(int &background_vol,
                                 std::vector<Tool> &tools)
{
  if (tools.empty()) {
    std::cout << "[info] No tools; skipping fragment.\n";
    return;
  }

  // Build dimtags list in same order as tools
  std::vector<DimTag> toolDims;
  toolDims.reserve(tools.size());
  for (auto &t : tools) toolDims.emplace_back(3, t.tag);

  // OCC fragment
  std::vector<DimTag> newEntities;
  std::vector<std::vector<DimTag>> prov;

  gmsh::model::occ::fragment({{3, background_vol}}, toolDims,
                             newEntities, prov,
                             /*tag=*/-1, /*removeObject=*/true, /*removeTool=*/false);

  // Updated background (bucket 0)
  if (!prov.empty() && !prov[0].empty())
    background_vol = prov[0][0].second;

  // Updated tool tags (bucket i+1 corresponds to tools[i])
  for (size_t i = 0; i < tools.size(); ++i)
    if (prov.size() > i + 1 && !prov[i + 1].empty())
      tools[i].tag = prov[i + 1][0].second;
}

#include <unordered_map>

void carveToolsByCutters(std::vector<Tool> &fluids,
                         std::vector<Tool> &cutters,
                         std::vector<Tool> &allTools)
{
  if (fluids.empty()) return;

  // Build OCC input lists
  std::vector<DimTag> objs; objs.reserve(fluids.size());
  std::vector<DimTag> tls;  tls.reserve(cutters.size());
  for (auto &o : fluids)  objs.emplace_back(3, o.tag);
  for (auto &c : cutters) tls.emplace_back(3, c.tag);

  std::vector<DimTag> newEntities;
  std::vector<std::vector<DimTag>> prov;

  // Objects = fluids (to be carved)
  // Tools   = cutters (electrodes, PTFE, etc.) that may be retagged by OCC
  gmsh::model::occ::fragment(objs, tls,
                             newEntities, prov,
                             /*tag=*/-1, /*removeObject=*/true, /*removeTool=*/false);

  // Build old->new tag map for both fluids and cutters
  std::unordered_map<int,int> old2new;

  const size_t m = fluids.size();
  for (size_t i = 0; i < m; ++i) {
    const int oldTag = fluids[i].tag;
    if (prov.size() > i && !prov[i].empty()) {
      const int newTag = prov[i][0].second;
      fluids[i].tag = newTag;
      old2new[oldTag] = newTag;
    }
  }
  for (size_t j = 0; j < cutters.size(); ++j) {
    const size_t k = m + j;
    const int oldTag = cutters[j].tag;
    if (prov.size() > k && !prov[k].empty()) {
      const int newTag = prov[k][0].second;
      cutters[j].tag = newTag;
      old2new[oldTag] = newTag;
    }
  }

  // Sync the master tools list by tag replacement
  // (fast, unambiguous: replaces any tag that appeared as a key)
  for (auto &t : allTools) {
    auto it = old2new.find(t.tag);
    if (it != old2new.end()) t.tag = it->second;
  }
}

std::vector<int> getSurfaces(int volTag)
{
  std::vector<DimTag> bdr;
  gmsh::model::getBoundary({{3, volTag}}, bdr,
                           /*combined=*/true, /*oriented=*/false, /*recursive=*/false);
  std::vector<int> out; out.reserve(bdr.size());
  for (auto &dt : bdr) if (dt.first == 2) out.push_back(dt.second);
  return out;
}

void tagPhysicals(const std::vector<Tool> &tools)
{
  // Gather by group id
  std::unordered_map<int, std::vector<int>> volGroups;   // volBC -> [volume tags]
  std::unordered_map<int, std::string>       volNames;   // volBC -> name (first seen)

  std::unordered_map<int, std::vector<int>> surfGroups;  // surfBC -> [surface tags]
  std::unordered_map<int, std::string>       surfNames;  // surfBC -> name (first seen)

  // Helper to get surfaces of a volume (reuse your existing getSurfaces)
  auto getSurfacesLocal = [](int volTag) {
    std::vector<DimTag> bdr;
    gmsh::model::getBoundary({{3, volTag}}, bdr,
                             /*combined=*/true,
                             /*oriented=*/false,
                             /*recursive=*/false);
    std::vector<int> out; out.reserve(bdr.size());
    for (auto &dt : bdr) if (dt.first == 2) out.push_back(dt.second);
    return out;
  };

  // Collect all entities into groups
  for (const auto &t : tools) {
    if (t.volBC >= 0) {
      volGroups[t.volBC].push_back(t.tag);
      if (!volNames.count(t.volBC)) volNames[t.volBC] = t.name + "_VOL";
    }
    if (t.surfBC >= 0) {
      std::vector<int> s = getSurfacesLocal(t.tag);
      auto &vec = surfGroups[t.surfBC];
      vec.insert(vec.end(), s.begin(), s.end());
      if (!surfNames.count(t.surfBC)) surfNames[t.surfBC] = t.name; // first wins
    }
  }

  // Deduplicate vectors
  auto dedup = [](std::vector<int> &v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
  };

  for (auto &kv : volGroups) dedup(kv.second);
  for (auto &kv : surfGroups) dedup(kv.second);

  // Create volume physical groups once per volBC
  for (const auto &kv : volGroups) {
    int tag = kv.first;
    const auto &ents = kv.second;
    if (!ents.empty()) {
      gmsh::model::addPhysicalGroup(3, ents, tag);
      auto it = volNames.find(tag);
      if (it != volNames.end() && !it->second.empty())
        gmsh::model::setPhysicalName(3, tag, it->second);
    }
  }

  // Create surface physical groups once per surfBC
  for (const auto &kv : surfGroups) {
    int tag = kv.first;
    const auto &ents = kv.second;
    if (!ents.empty()) {
      gmsh::model::addPhysicalGroup(2, ents, tag);
      auto it = surfNames.find(tag);
      if (it != surfNames.end() && !it->second.empty())
        gmsh::model::setPhysicalName(2, tag, it->second);
    }
  }
}

static std::unordered_set<int> currentVolumeTags() {
  std::vector<DimTag> vols;
  gmsh::model::getEntities(vols, 3);
  std::unordered_set<int> s;
  for (auto &v : vols) s.insert(v.second);
  return s;
}

static int countSurfacesOfVolume(int volTag) {
  std::vector<DimTag> bdr;
  gmsh::model::getBoundary({{3, volTag}}, bdr, /*combined=*/true, /*oriented=*/false, /*recursive=*/false);
  int n = 0; for (auto &dt : bdr) if (dt.first == 2) ++n; return n;
}

void printRegisteredTools(const std::vector<Tool> &tools,
                          const std::string &title)
{
  auto volSet = currentVolumeTags();

  std::cout << "\n=== " << title << " ===\n";
  std::cout << std::left
            << std::setw(6)  << "Idx"
            << std::setw(14) << "Name"
            << std::setw(10) << "VolTag"
            << std::setw(9)  << "Exists"
            << std::setw(10) << "#Surfs"
            << std::setw(10) << "SurfBC"
            << std::setw(10) << "VolBC"
            << "\n";

  std::cout << std::string(6+14+10+9+10+10+10, '-') << "\n";

  for (size_t i = 0; i < tools.size(); ++i) {
    const auto &t = tools[i];
    const bool exists = volSet.count(t.tag) > 0;
    int nSurfs = exists ? countSurfacesOfVolume(t.tag) : 0;

    std::cout << std::left
              << std::setw(6)  << i
              << std::setw(14) << t.name
              << std::setw(10) << t.tag
              << std::setw(9)  << (exists ? "yes" : "no")
              << std::setw(10) << nSurfs
              << std::setw(10) << (t.surfBC >= 0 ? std::to_string(t.surfBC) : "-")
              << std::setw(10) << (t.volBC  >= 0 ? std::to_string(t.volBC)  : "-")
              << "\n";
  }
  std::cout << std::string(6+14+10+9+10+10+10, '-') << "\n";
}


} // namespace tpc::geom
