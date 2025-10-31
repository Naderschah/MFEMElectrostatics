#include "boundary_conditions.h"


// Build a safe boundary marker for the given attribute IDs.
static Array<int> MakeBdrMarker(const Mesh *mesh,
                                std::vector<int> attrs)
{
    int max_id = mesh->bdr_attributes.Size() ? mesh->bdr_attributes.Max() : 0;
    Array<int> m(std::max(1, max_id)); // at least length 1, never 0
    m = 0;
    for (int a : attrs)
    {
        if (mesh->bdr_attributes.Find(a) != -1) { m[a - 1] = 1; }
        else { std::cerr << "[BC] Warning: boundary attribute " << a
                         << " not found in mesh.\n"; }
    }
    return m;
}

// Identifies which boundaries should have Dirichlet conditions
// TODO What were this markers again and what makes them distinct from the ids? 
Array<int> GetDirichletAttributes(Mesh *mesh, const std::shared_ptr<const Config>& cfg)
{
    // Collect all Dirichlet boundary IDs from the config
    std::vector<int> dirichlet_ids;
    dirichlet_ids.reserve(cfg->boundaries.size());

    for (const auto& [name, bc] : cfg->boundaries)
    {
        if (bc.type == "dirichlet")
        {
            dirichlet_ids.push_back(bc.bdr_id);
        }
    }
    // Get Bdr Markers 
    Array<int> ess = MakeBdrMarker(mesh, dirichlet_ids);
    return ess;
}


void ApplyDirichletValues(GridFunction &V, const Array<int> &dirichlet_attr, const std::shared_ptr<const Config>& cfg)
{
    Mesh *mesh = V.FESpace()->GetMesh();
    // Iterate config elements with dirichlet attributes
    for (const auto& [name, bc] :cfg->boundaries)
    {
      if (bc.type != "dirichlet") continue; 

      Array<int> marker = MakeBdrMarker(mesh, {bc.bdr_id});
      if (marker.Max() == 1)
      {
        ConstantCoefficient Vcoef(bc.value);
        V.ProjectBdrCoefficient(Vcoef, marker);
        if (cfg->debug.debug) {
          std::cout << "Applied Dirichlet BC " << name << " '(bdr_id = " << bc.bdr_id << ")'\n";
        }
      }
      else 
      {
        std::cerr << "\033[33m" << "WARNING: boundary " << name << "'(bdr_id= " << bc.bdr_id << ")' not present in mesh!\n" << "\033[0m\n";
      }
    }
}
