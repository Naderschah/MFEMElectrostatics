#define MFEM_DEBUG
#include "mfem.hpp"
#include "load_mesh.h"
#include "boundary_conditions.h"
#include "solver.h"
#include "ComputeElectricField.h"
#include "config/Config.h"
#include "cmdLineParser.h"

#include <iostream>
#include <memory>

using namespace mfem;

int main(int argc, char *argv[])
{
    // Extract cmd line options 
    cli::InputParser args(argc, argv);
    if (args.has("-h") || args.has("--help")) {
        cli::print_usage(argv[0]);
        return 0;
    }

    //----------------------   Read options ---------------------------
    // config path 
    auto config_str_opt = args.get("-c");
    if (!config_str_opt) config_str_opt = args.get("--config");
    if (!config_str_opt) {
        std::cerr << "Error: missing required argument -c/--config\n";
        cli::print_usage(argv[0]);
        return 1;
    }
    auto config_path = cli::to_absolute(*config_str_opt);
    if (!std::filesystem::exists(config_path)) {
        std::cerr << "Error: config file not found: " << config_path << "\n";
        return 1;
    }
    // mesh path 
    auto model_str_opt = args.get("-m");
    if (!model_str_opt) model_str_opt = args.get("--model");
    if (!model_str_opt) {
        std::cerr << "Error: missing required argument -m/--model\n";
        cli::print_usage(argv[0]);
        return 1;
    }
    auto model_path  = cli::to_absolute(*model_str_opt);
    if (!std::filesystem::exists(model_path)) {
        std::cerr << "Error: model/mesh file not found: " << model_path << "\n";
        return 1;
    }

    // Log and continue 
    std::cout << "[Config] " << config_path << "\n";
    std::cout << "[Mesh]   " << model_path  << "\n";

    // Load yaml config containing geometry and solver parameters
    auto cfg = std::make_shared<const Config>(
        Config::Load(config_path)
    );

    // 1. Create the mesh
    Mesh* mesh = CreateSimulationDomain(model_path); //cfg->mesh.path
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

