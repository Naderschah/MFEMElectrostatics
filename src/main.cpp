#define MFEM_DEBUG
#include "mfem.hpp"
#include "load_mesh.h"
#include "boundary_conditions.h"
#include "solver.h"
#include "ComputeElectricField.h"
#include "config/Config.h"

#include <memory>

using namespace mfem;

int main(int argc, char *argv[])
{
    // 0. Load the yaml config for the geomtry 
    std::string config_path = "config/config.yaml";     
    auto cfg = std::make_shared<const Config>(
        Config::Load(config_path)
    );
    std::cout << "[Config] Loading from: " << config_path << std::endl;

    // 1. Create the mesh
    Mesh* mesh = CreateSimulationDomain(cfg->mesh.path);
    if (cfg->debug.debug)
    {
        std::cout << "bdr_attributes Max=" << mesh->bdr_attributes.Max()
            << " list: ";
        for (int i = 0; i < mesh->bdr_attributes.Size(); i++)
            std::cout << mesh->bdr_attributes[i] << " ";
        std::cout << "\nNBE=" << mesh->GetNBE() << "\n";

        int count1=0, count2=0, count3=0;
        for (int be = 0; be < mesh->GetNBE(); be++)
        {
            int a = mesh->GetBdrElement(be)->GetAttribute();
            if (a == 1) count1++;
            if (a == 2) count2++;
            if (a == 3) count3++;
        }
        std::cout << "boundary counts: attr1=" << count1
                << " attr2=" << count2
                << " attr3=" << count3 << "\n";

        std::cout << "Mesh dimensions: " 
                << mesh->GetNE() << " elements, "
                << mesh->GetNBE() << " boundary elements" << std::endl;
    }
    // 2. Create finite element collection and space
    H1_FECollection fec(cfg->solver.order, mesh->Dimension());
    FiniteElementSpace fespace(mesh, &fec);

    // 3. Get Dirichlet boundary attributes
    Array<int> dirichlet_arr = GetDirichletAttributes(mesh, cfg);

    // 4. Solve Poisson
    GridFunction V = SolvePoisson(fespace, dirichlet_arr, cfg);

    // Vector-valued FE space for E-field
    FiniteElementSpace *vec_fes = new FiniteElementSpace(mesh, &fec, mesh->Dimension());
    // Scalar FE space for |E|
    FiniteElementSpace *scalar_fes = new FiniteElementSpace(mesh, &fec, 1);

    // Allocate GridFunctions
    GridFunction E(vec_fes);     // vector field
    GridFunction Emag(scalar_fes); // magnitude

    // Compute vector E-field and its magnitude
    ComputeElectricField(V, E);      // fills E with -∇V
    ComputeFieldMagnitude(E, Emag);  // computes |E| at nodes

    // 5. Save Data
    mesh->Save(cfg->solver.mesh_save_path.c_str());
    V.Save(cfg->solver.V_solution_path.c_str());
    Emag.Save(cfg->solver.Emag_solution_path.c_str());

}

