
#include "mfem.hpp"
#include "mesh_tools.h"
#include "boundary_conditions.h"
#include "solver.h"
#include "ComputeElectricField.h"
#include "Config.h"
#include "tmop_tools.h"
#include "cmdLineInteraction.h"
#include "solver_api.h"
#include "mesh_tools.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <optional>
#include <omp.h>
#include <filesystem>
#include <chrono>
#include <vector>


using namespace mfem;

namespace
{
void SyncResolvedConfigToYaml(const Config &cfg, YAML::Node &yaml_root)
{
    if (!yaml_root["boundaries"] || !yaml_root["boundaries"].IsMap())
    {
        yaml_root["boundaries"] = YAML::Node(YAML::NodeType::Map);
    }
    YAML::Node boundaries = yaml_root["boundaries"];

    for (const auto &[name, boundary] : cfg.boundaries)
    {
        if (!boundaries[name] || !boundaries[name].IsMap())
        {
            boundaries[name] = YAML::Node(YAML::NodeType::Map);
        }

        YAML::Node dst = boundaries[name];
        dst["bdr_id"] = boundary.bdr_id;
        dst["type"] = boundary.type;
        if (std::isfinite(boundary.value))
        {
            dst["value"] = boundary.value;
        }
        dst["depth_dependent"] = boundary.depth_dependent;
        if (boundary.depth_dependent)
        {
            dst["z_bot"] = boundary.z_bot;
            dst["z_top"] = boundary.z_top;
            dst["value_bot"] = boundary.value_bot;
            dst["value_top"] = boundary.value_top;
        }
    }

    if (!cfg.mesh.path.empty())
    {
        if (!yaml_root["mesh"] || !yaml_root["mesh"].IsMap())
        {
            yaml_root["mesh"] = YAML::Node(YAML::NodeType::Map);
        }
        yaml_root["mesh"]["path"] = cfg.mesh.path;
    }
}

} // namespace

std::string PrecomputeAMRMesh(const Config &cfg,
                              int precision)
{
    // TODO Config saving as this is voltage dependent, might be worthwhile to maximize delta V in an optimization
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    std::chrono::steady_clock::time_point t_start;
    auto cfg_ptr = std::make_shared<Config>(cfg);

    const std::filesystem::path out_dir  = cfg.save_path;
    const std::filesystem::path mesh_dir = out_dir / "amr_mesh";

    if (cfg.debug.timing) t_start = std::chrono::steady_clock::now();
    SimulationResult res = run_simulation(cfg_ptr, cfg.mesh.path, false);
    if (cfg.debug.timing)
    {
        auto t_end = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
        std::cout << "[Timing]: AMR timing (" << dt << " ms)" << std::endl;
    }

    if (rank == 0 && !res.adaptive_converged)
    {
        std::cout
            << "\n[AMR WARNING] Precompute stopped before convergence.\n"
            << "  adaptive_iterations=" << res.adaptive_iterations
            << ", iteration_limit_hit="
            << (res.adaptive_iteration_limit_reached ? "true" : "false") << "\n"
            << "  mesh.AMR_TMOP_iter currently limits adaptive solve passes.\n"
            << "  Increase mesh.AMR_TMOP_iter (and/or mesh.AMR.max_iter) if you want fully converged precompute.\n"
            << "  Runtime will use the precomputed AMR mesh without additional AMR refinement.\n\n";
    }

    SaveAMRMeshArtifacts(*res.mesh, mesh_dir, cfg.mesh.amr, precision);

    if (rank == 0)
    {
        std::cout << "[AMR] wrote mesh artifacts in: " << mesh_dir << "\n"
                  << "      - parallel MFEM pieces: enabled\n"
                  << "      - serial MFEM mesh: "
                  << (cfg.mesh.amr.save_serial ? "enabled (amr_mesh_serial.mesh)" : "disabled") << "\n"
                  << "      - parallel VTU/PVTU: "
                  << (cfg.mesh.amr.save_vtu_parallel ? "enabled (amr_mesh_vtu.pvtu + amr_mesh_vtu_XXXXXX.vtu)" : "disabled")
                  << "\n";
    }

    return mesh_dir.string();
}

SimulationResult run_simulation(std::shared_ptr<Config> cfg,
                                const std::filesystem::path& model_path,
                                bool skip_amr = false)
{
  if (model_path.empty())
  {
    throw std::runtime_error(
      "run_simulation: empty mesh/model path. "
      "Set mesh.path or resolve mesh generation before simulation.");
  }

  // What will this default to if I do nothing with MPI
  MPI_Comm comm = MPI_COMM_WORLD;

  // Solve voltages in circuit
  apply_fieldcage_network(*cfg);

  // Create the mesh
  auto mesh = CreateSimulationDomain(model_path, comm); 
  if (cfg->solver.axisymmetric) {
    if (mesh->Dimension() != 2) { std::cerr << "Axisymmetric Simulation but Geometry is not 2D" << std::endl;  }
    // Check r axis starts at null
    CheckAxisymmetricMesh(*mesh,0);
  }

  auto fec  = std::make_unique<mfem::H1_FECollection>(cfg->solver.order, mesh->Dimension());
  auto pfes = std::make_unique<mfem::ParFiniteElementSpace>(mesh.get(), fec.get());

  BoundaryConditionGroups BCs = GetBoundaryConditionGroups(mesh.get(), cfg);

  // Solve Mesh and AMR 
  std::unique_ptr<mfem::ParGridFunction> V;

  const int max_iter = cfg->mesh.AMR_TMOP_iter;
  bool adaptive_converged = false;
  int adaptive_iterations = 0;
  bool changed_on_last_iteration = false;
  std::string solver_log_overwrite = 
    cfg->mesh.amr.enable ? (std::filesystem::path(cfg->save_path) / "amr_mesh").string() : "";
  for (int it = 0; it < max_iter; ++it)
  {
    // Solve Poisson
    V = SolvePoisson(*pfes, BCs, cfg, solver_log_overwrite);

    bool amr_changed = false;
    if (cfg->mesh.amr.enable && !skip_amr)
    {
      amr_changed = ApplyAMRRefineDerefineStep(*mesh, *pfes, *V, *cfg);
    }

    bool tmop_changed = false;
    if (cfg->mesh.tmop.enable)
    {
      tmop_changed = ApplyTMOPStep(*mesh, *cfg);
    }

    changed_on_last_iteration = (amr_changed || tmop_changed);
    adaptive_iterations = it + 1;

    if (!changed_on_last_iteration)
    {
      adaptive_converged = true;
      break;
    }

    pfes->Update();
    V->Update();
    pfes->UpdatesFinished();
    BCs = GetBoundaryConditionGroups(mesh.get(), cfg);
  }


  SimulationResult result;

  // Postprocessing
  if (cfg->solver.generate_E) 
  {
    mfem::ParMesh *pmesh = mesh.get(); 
    InitFieldPostprocessor(*pfes);

    std::unique_ptr<mfem::ParGridFunction> E    = CreateE();  
    std::unique_ptr<mfem::ParGridFunction> Emag = CreateEmag();  

    // Compute E and |E|
    ComputeElectricField(*V, *E, /*scale=*/-1.0); // V/m
    ComputeFieldMagnitude(*E, *Emag);

    // Transfer ownership
    result.E    = std::move(E);
    result.Emag = std::move(Emag);
  }
  result.mesh = std::move(mesh);
  result.pfes = std::move(pfes);
  result.fec  = std::move(fec);
  result.V    = std::move(V);
  result.adaptive_converged = adaptive_converged;
  result.adaptive_iteration_limit_reached =
    (!adaptive_converged && (adaptive_iterations >= max_iter) && changed_on_last_iteration);
  result.adaptive_iterations = adaptive_iterations;
  result.success = true;

  return result;
}

// This was getting too annoying on load, wait for a miniapp to come out, i couldnt get the partition to wrok
static void save_results_serial(const SimulationResult &result, const std::filesystem::path &root_path)
{
    int rank = 0;
    MPI_Comm comm = result.mesh->GetComm();
    MPI_Comm_rank(comm, &rank);
    // --- Create directory structure ---
    std::error_code ec;
    if (rank == 0){
    std::filesystem::create_directories(root_path, ec);
    if (ec) {
        std::cerr << "Warning: could not create output directory "
                  << root_path << " : " << ec.message() << "\n";
    }}
    MPI_Barrier(comm);

    // --- Save E field TODO Add flag in config ---
    {
        result.E->SaveAsSerial((root_path / "E.gf").c_str(), 16, 0);
        result.Emag->SaveAsSerial((root_path / "Emag.gf").c_str(), 16, 0);
    }

    // Save mesh as serial (collective; rank 0 writes)
    std::filesystem::path mesh_path = root_path / "simulation_mesh.msh";
    std::ofstream mesh_out;
    if (rank == 0) { mesh_out.open(mesh_path); }

    int ok = 1;
    if (rank == 0) { ok = mesh_out.is_open() ? 1 : 0; }
    MPI_Bcast(&ok, 1, MPI_INT, 0, comm);
    MFEM_VERIFY(ok == 1, "Could not open mesh file on rank 0.");

    result.mesh->PrintAsSerial(mesh_out);
    
    result.V->SaveAsSerial((root_path / "V.gf").c_str(), 16, 0);

    // ---------- ParaView output ----------
    // Collection name appears in the .pvd file
    mfem::ParaViewDataCollection pvdc("Simulation", result.mesh.get());
    pvdc.SetPrefixPath(root_path.string());

    auto RegisterIfReady = [&](const char* name,
                           const std::unique_ptr<mfem::ParGridFunction>& gf)
    {
        if (!gf) return;

        // Basic sanity: MFEM grid functions should have a space and a vector size.
        if (!gf->ParFESpace()) return;
        if (gf->Size() <= 0) return;

        pvdc.RegisterField(name, gf.get());
    };

    RegisterIfReady("V",    result.V);
    RegisterIfReady("E",    result.E);
    RegisterIfReady("Emag", result.Emag);

    int order = result.V->FESpace()->GetOrder(0);
    pvdc.SetLevelsOfDetail(order);
    pvdc.SetDataFormat(mfem::VTKFormat::BINARY);
    pvdc.SetHighOrderOutput(true);

    pvdc.Save(); // call on all ranks
}
void save_results(const SimulationResult &result, const std::filesystem::path &root_path, YAML::Node yaml_config)
{
    MFEM_VERIFY(result.mesh, "save_results: result.mesh is null");
    MPI_Comm comm = result.mesh->GetComm();

    int rank = 0, size = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    // --- Create output directory (rank 0) ---
    if (rank == 0)
    {
        std::error_code ec;
        std::filesystem::create_directories(root_path, ec);
        if (ec)
        {
            std::cerr << "Warning: could not create output directory "
                      << root_path << " : " << ec.message() << "\n";
        }
    }
    MPI_Barrier(comm);

    // -------------------------------------------------------------------------
    // 1) Save a PARALLEL mesh (each rank writes its piece)
    // -------------------------------------------------------------------------
    SaveParallelMesh(*result.mesh, root_path, "simulation_pmesh", 16);
    // -------------------------------------------------------------------------
    // 2) Save ParGridFunctions in PARALLEL form (each rank writes its piece)
    // -------------------------------------------------------------------------
    auto SaveParGF = [&](const char *base, const std::unique_ptr<mfem::ParGridFunction> &gf)
    {
        if (!gf) { return; }
        MFEM_VERIFY(gf->ParFESpace(), "save_results: ParGridFunction has no ParFESpace");
        MFEM_VERIFY(gf->Size() > 0, "save_results: ParGridFunction has invalid size");

        std::ostringstream fname;
        fname << (root_path / base).string()
            << ".pgf."
            << std::setw(6) << std::setfill('0') << rank;

        std::ofstream out(fname.str());
        MFEM_VERIFY(out.good(), "save_results: could not open ParGridFunction output file");

        out.precision(16);
        out.setf(std::ios::scientific);

        // Vector::Load(in) expects the size first. :contentReference[oaicite:4]{index=4}
        out << gf->Size() << '\n';

        // Print entries only (Vector::Print does NOT print size). :contentReference[oaicite:5]{index=5}
        gf->Print(out, 8);
    };

    SaveParGF("V",    result.V);
    SaveParGF("E",    result.E);
    SaveParGF("Emag", result.Emag);

    // -------------------------------------------------------------------------
    // 3) ParaView output (.pvtu + per-rank .vtu)
    // -------------------------------------------------------------------------
    // This remains the recommended visualization output.
    mfem::ParaViewDataCollection pvdc("Simulation", result.mesh.get());
    pvdc.SetPrefixPath(root_path.string());

    auto RegisterIfReady = [&](const char *name,
                               const std::unique_ptr<mfem::ParGridFunction> &gf)
    {
        if (!gf) { return; }
        if (!gf->ParFESpace()) { return; }
        if (gf->Size() <= 0) { return; }
        pvdc.RegisterField(name, gf.get());
    };

    RegisterIfReady("V",    result.V);
    RegisterIfReady("E",    result.E);
    RegisterIfReady("Emag", result.Emag);

    // Choose LoD from V if available
    if (result.V && result.V->ParFESpace())
    {
        const int order = result.V->ParFESpace()->GetOrder(0);
        pvdc.SetLevelsOfDetail(order);
    }
    pvdc.SetDataFormat(mfem::VTKFormat::BINARY);
    pvdc.SetHighOrderOutput(true);

    pvdc.Save(); // collective

    // --- Dump YAML config as root_path/config.yaml (rank 0 only) ---
    if (rank == 0)
    {
        YAML::Emitter out;
        out << yaml_config;

        const std::filesystem::path cfg_path = root_path / "config.yaml";
        std::ofstream cfg_os(cfg_path);
        MFEM_VERIFY(cfg_os.good(), "save_results: could not open config.yaml for writing");

        cfg_os << out.c_str() << '\n';
        cfg_os.close();
    }
    MPI_Barrier(comm);
}


namespace
{
std::optional<std::filesystem::path>
find_first_with_prefix(const std::filesystem::path &dir,
                       const std::string &prefix)
{
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        return std::nullopt;
    }

    for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;

        const std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {  // starts with prefix
            return entry.path();
        }
    }
    return std::nullopt;
}
} // namespace

// We need to match the partitioning of the ParMesh for any Par Objects we create s
static mfem::Array<int> BuildElementPartitioningFromParMesh(mfem::ParMesh &pmesh)
{
    MPI_Comm comm = pmesh.GetComm();
    int rank = 0, size = 1;
    MPI_Comm_rank(comm, &rank);
    MPI_Comm_size(comm, &size);

    const long long gNE_ll = pmesh.GetGlobalNE();
    MFEM_VERIFY(gNE_ll > 0, "Invalid global number of elements.");
    MFEM_VERIFY(gNE_ll <= std::numeric_limits<int>::max(), "Global NE too large for int.");
    const int gNE = (int)gNE_ll;

    mfem::Array<int> part(gNE);
    part = -1;

    const int lNE = pmesh.GetNE(); // locally-owned elements
    for (int le = 0; le < lNE; ++le)
    {
        const long long ge_ll = pmesh.GetGlobalElementNum(le);
        MFEM_VERIFY(ge_ll >= 0 && ge_ll < gNE_ll, "Invalid global element id.");
        part[(int)ge_ll] = rank;
    }

    // Each global element is owned by exactly one rank => MAX works
    MPI_Allreduce(MPI_IN_PLACE, part.GetData(), gNE, MPI_INT, MPI_MAX, comm);

    // sanity
    for (int ge = 0; ge < gNE; ++ge)
    {
        MFEM_VERIFY(part[ge] >= 0 && part[ge] < size, "Unassigned element owner.");
    }

    return part;
}


static SimulationResult load_results_old(const Config &cfg,
                              const std::filesystem::path &root_path)
{
    using namespace mfem;

    SimulationResult result;
    result.success = false;

    // --- Explicit filenames (no prefix search) ---
    const auto mesh_path = root_path / "simulation_mesh.msh";
    const auto V_path    = root_path / "V.gf";

    if (!std::filesystem::exists(mesh_path))
    {
        result.error_message = "load_results: missing " + mesh_path.string();
        return result;
    }
    if (!std::filesystem::exists(V_path))
    {
        result.error_message = "load_results: missing " + V_path.string();
        return result;
    }

    // --- 1) Load SERIAL mesh ---
    Mesh serial_mesh;
    {
        std::ifstream mesh_in(mesh_path);
        if (!mesh_in)
        {
            result.error_message = "load_results: cannot open " + mesh_path.string();
            return result;
        }
        serial_mesh.Load(mesh_in, /*generate_edges=*/0, /*refine=*/0);
    }

    // --- 2) Build SERIAL space + load SERIAL V ---
    auto fec_h1_s  = std::make_unique<H1_FECollection>(cfg.solver.order, serial_mesh.Dimension());
    auto fes_h1_s  = std::make_unique<FiniteElementSpace>(&serial_mesh, fec_h1_s.get(), 1);

    std::unique_ptr<mfem::GridFunction> V_s;
    std::ifstream vin(V_path);
    if (!vin)
    {
        result.error_message = "load_results: cannot open " + V_path.string();
        return result;
    }
    V_s = std::make_unique<mfem::GridFunction>(&serial_mesh, vin);

    // --- 3) Convert to PAR objects required by SimulationResult ---
    result.mesh = std::make_unique<mfem::ParMesh>(MPI_COMM_WORLD, serial_mesh);
    
    result.fec  = std::make_unique<mfem::H1_FECollection>(cfg.solver.order, result.mesh->Dimension());
    result.pfes = std::make_unique<mfem::ParFiniteElementSpace>(result.mesh.get(), result.fec.get(), 1);

    // Build parallel phi
    result.V = std::make_unique<mfem::ParGridFunction>(result.pfes.get());

    // Interpolate serial V_s into the parallel space
    mfem::GridFunctionCoefficient Vcoeff(V_s.get());
    result.V->ProjectCoefficient(Vcoeff);

    // Make sure shared DOFs are consistent
    result.V->ExchangeFaceNbrData();

    // --- 4) Rebuild E/Emag from components as you already do ---
    try
    {
        ElectricFieldPostprocessor efpp(*result.pfes);
        std::unique_ptr<ParGridFunction> E = efpp.MakeE();
        efpp.LoadE(*E, (root_path / "E").string());
        result.E = std::move(E);

        std::unique_ptr<ParGridFunction> Emag = efpp.MakeEmag();
        efpp.ComputeFieldMagnitude(*result.E, *Emag);
        result.Emag = std::move(Emag);
    }
    catch (const std::exception &e)
    {
        // leave E/Emag null; V + mesh valid
        std::cerr << "load_results: failed to reconstruct E/Emag: " << e.what() << "\n";
    }

    result.success = true;
    result.error_message.clear();
    return result;
}
// TODO Modify cant handle run_000n directories yet
SimulationResult load_results(const Config &cfg,
                              const std::filesystem::path &root_path)
{
    using namespace mfem;

    SimulationResult result;
    result.success = false;

    int rank = 0, size = 1;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // ---------------------------------------------------------------------
    // 1) Load PARALLEL mesh (each rank reads its own piece)
    // ---------------------------------------------------------------------
    const std::string mesh_prefix = (root_path / "simulation_pmesh").string();

    std::ostringstream mesh_fname;
    mesh_fname << mesh_prefix << "."
               << std::setw(6) << std::setfill('0') << rank;

    if (!std::filesystem::exists(mesh_fname.str()))
    {
        result.error_message =
            "load_results: missing " + mesh_fname.str() +
            " (saved with different MPI size or wrong prefix?)";
        return result;
    }

    try
    {
        std::ifstream mesh_in(mesh_fname.str());
        if (!mesh_in)
        {
            result.error_message = "load_results: cannot open " + mesh_fname.str();
            return result;
        }
        result.mesh = std::make_unique<mfem::ParMesh>(MPI_COMM_WORLD, mesh_in);
    }
    catch (const std::exception &e)
    {
        result.error_message = std::string("load_results: failed to load parallel mesh: ") + e.what();
        return result;
    }

    // ---------------------------------------------------------------------
    // 2) Build parallel FE space for V (phi)
    // ---------------------------------------------------------------------
    const int dim = result.mesh->Dimension();
    result.fec  = std::make_unique<mfem::H1_FECollection>(cfg.solver.order, dim);
    result.pfes = std::make_unique<mfem::ParFiniteElementSpace>(result.mesh.get(),
                                                                result.fec.get(), 1);

    // Helper to load a per-rank ParGridFunction file into the given pfes
    auto load_pgf = [&](const char *base,
                    std::unique_ptr<mfem::ParGridFunction> &out) -> bool
    {
        std::ostringstream fname;
        fname << (root_path / base).string()
            << ".pgf."
            << std::setw(6) << std::setfill('0') << rank;

        if (!std::filesystem::exists(fname.str()))
        {
            return false;
        }

        std::ifstream in(fname.str());
        if (!in) { return false; }

        out = std::make_unique<mfem::ParGridFunction>(result.pfes.get());

        // --- Read size header (Vector::Load expects size first if using Load(in)) ---
        int file_size = -1;
        in >> file_size;
        if (!in || file_size < 0)
        {
            // Bad file / wrong format
            out.reset();
            return false;
        }

        const int expected = out->Size();
        if (file_size != expected)
        {
            // Most common causes: different polynomial order, different dim,
            // different FE space, or different partitioning than expected.
            out.reset();
            return false;
        }

        // Load the raw vector entries
        out->Load(in, file_size);  // mfem::Vector::Load(std::istream&, int) :contentReference[oaicite:3]{index=3}

        // Optional, only if you need face-neighbor data later
        out->ExchangeFaceNbrData();
        return true;
    };

    // ---------------------------------------------------------------------
    // 3) Load V (required)
    // ---------------------------------------------------------------------
    if (!load_pgf("V", result.V))
    {
        result.error_message =
            "load_results: missing V.pgf.<rank> in " + root_path.string();
        return result;
    }

    // ---------------------------------------------------------------------
    // 4) Load E and/or Emag (optional) + Emag rebuild if only E exists
    // ---------------------------------------------------------------------
    // If you saved E/Emag as parallel pgf files, load them.
    // Otherwise, we do NOT compute E here (no ElectricFieldCoeff in this module).
    const bool haveE    = load_pgf("E", result.E);
    const bool haveEmag = load_pgf("Emag", result.Emag);

    // If Emag not saved but E is available, compute Emag from E (same tool you use)
    if (!haveEmag && haveE)
    {
        try
        {
            ElectricFieldPostprocessor efpp(*result.pfes);
            auto Emag = efpp.MakeEmag();
            efpp.ComputeFieldMagnitude(*result.E, *Emag);
            Emag->ExchangeFaceNbrData();
            result.Emag = std::move(Emag);
        }
        catch (const std::exception &e)
        {
            if (rank == 0)
            {
                std::cerr << "load_results: failed to rebuild Emag from E: "
                          << e.what() << "\n";
            }
        }
    }

    result.success = true;
    result.error_message.clear();
    return result;
}


// Single Simulation with file path handling 

std::string run_one(const Config &cfg,
                    YAML::Node yaml_root, 
                    const std::vector<std::pair<std::string, std::string>> &active_params,
                    std::size_t run_index,
                    bool skip_amr)
{
    std::chrono::steady_clock::time_point t_start;
    if (cfg.debug.timing) t_start = std::chrono::steady_clock::now();
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    int world_size = 0;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    // Dispatch
    apply_fieldcage_network(yaml_root);
    Config cfg_copy = Config::LoadFromNode(yaml_root);
    if (cfg_copy.mesh.path.empty())
    {
        throw std::runtime_error(
            "run_one: mesh.path is empty after YAML/config resolution. "
            "Provide mesh.path or mesh generation metadata.");
    }

    std::filesystem::path model_path = cfg_copy.mesh.path;
    const std::filesystem::path run_dir(cfg_copy.save_path);
    std::string run_name = run_dir.filename().string();

    auto cfg_ptr = std::make_shared<Config>(cfg_copy);
    MPI_Barrier(MPI_COMM_WORLD);
    if (!cfg_copy.debug.dry_run)
    {
        if (cfg_copy.debug.timing) t_start = std::chrono::steady_clock::now();
        SimulationResult result = run_simulation(cfg_ptr, model_path, skip_amr);
        if (cfg_copy.debug.timing)
        {
            auto t_end = std::chrono::steady_clock::now();
            auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
            std::cout << "[Timing]: Single Simulation timing (" << dt << " ms)" << std::endl;
        }
        if (!result.success)
        {
            std::cerr << "Simulation failed for " << run_name
                      << ": " << result.error_message << "\n";
            return {}; 
        }
        SyncResolvedConfigToYaml(*cfg_ptr, yaml_root);
        save_results(result, cfg_copy.save_path, yaml_root);
    }

    if (cfg_copy.debug.printBoundaryConditions){
        std::cout << "[DEBUG] Boundary Condition values" << std::endl;
        for (const auto& [name, b] : cfg_copy.boundaries) {
            std::cout << name << ": " << b.value << '\n';
        }
    }
    if (cfg_copy.debug.timing)
    {
        auto t_end = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
        std::cout << "[Timing]: run_one timing (" << dt << " ms)" << std::endl;
    }
    return run_name;
}
