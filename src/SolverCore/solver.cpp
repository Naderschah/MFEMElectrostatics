#include "solver.h"

#include <cmath>
#include <filesystem>
#include <iomanip>
#include "HYPRE_parcsr_ls.h"

// Writing residuals to file
ResidualFileMonitor::ResidualFileMonitor(std::ostream &os)
    : out_(os)
{
}

void ResidualFileMonitor::MonitorResidual(int it,
                                          mfem::real_t norm,
                                          const mfem::Vector &,
                                          bool final)
{
    out_ << it << " " << std::scientific << norm;
    if (final)
    {
        out_ << " final";
    }
    out_ << std::endl;
}
void ResidualFileMonitor::MonitorSolution(int,
                                          mfem::real_t,
                                          const mfem::Vector&,
                                          bool)
{
    // no-op
}





// Internal Helper for axisymmetric
inline std::unique_ptr<mfem::Coefficient>
MakeAxisymWeightCoeff(bool axisymmetric, int radial_idx)
{
  if (!axisymmetric) {
    return std::make_unique<mfem::ConstantCoefficient>(1.0);
  }

  // FunctionCoefficient expects physical coords X
  auto w_of_X = [radial_idx](const mfem::Vector &X) -> double {
    double r = X(radial_idx);
    if (r < 0.0) r = 0.0;               // guard tiny negatives from roundoff
    return 2.0 * M_PI * r;
  };
  return std::make_unique<mfem::FunctionCoefficient>(w_of_X);
}

// Internal Helper for distributed
inline bool IsDistributed(const mfem::FiniteElementSpace &fes)
{
  return dynamic_cast<const mfem::ParFiniteElementSpace*>(&fes) != nullptr;
}


// Build ε(x) that is piecewise-constant over element attributes (volume tags)
static PWConstCoefficient BuildEpsilonPWConst(const Mesh &mesh, const std::shared_ptr<const Config>& cfg)
{
    // https://physics.nist.gov/cgi-bin/cuu/Value?ep0
    constexpr double eps0 = 8.8541878188e-12;

    MFEM_VERIFY(mesh.attributes.Size() > 0,
                "Mesh has no volume attributes; cannot build permittivity coefficient.");

    // attributes are 1-based; we need a vector sized by the max attribute id
    const int max_attr = mesh.attributes.Max();  // e.g. 2004 in your case
    MFEM_VERIFY(max_attr > 0, "Invalid mesh volume attribute range.");
    Vector eps_by_attr(max_attr);
    eps_by_attr = 0.0;

    // helper that writes using 1-based attributes
    auto set_eps = [&](int attr, double val)
    {
        MFEM_VERIFY(attr >= 1 && attr <= max_attr,
                    "Volume attribute id out of range.");
        eps_by_attr(attr - 1) = val;
    };

    // Set all materials epsilon from config 
    for (const auto& [name, mat] : cfg->materials)
    {
      // Skip materials that have no explicit attr_id (e.g. "Default")
      if (mat.id > 0)
      {
        MFEM_VERIFY(std::isfinite(mat.epsilon_r) && mat.epsilon_r > 0.0,
                    "Invalid epsilon_r for material '" << name
                    << "': epsilon_r must be finite and > 0.");
        set_eps(mat.id, mat.epsilon_r * eps0);
      }
      else if (name == "Default")
      {
        MFEM_VERIFY(std::isfinite(mat.epsilon_r) && mat.epsilon_r > 0.0,
                    "Invalid epsilon_r for material 'Default': epsilon_r must be finite and > 0.");
        // Fill in any remaining attributes that were not explicitly set
        for (int a = 1; a <= max_attr; ++a)
        {
          if (eps_by_attr(a - 1) == 0.0)  eps_by_attr(a - 1) = mat.epsilon_r * eps0;
        }
      }
    }

    // Validate that every volume attribute present in the mesh has a valid epsilon.
    int bad_attr = -1;
    for (int i = 0; i < mesh.attributes.Size(); ++i)
    {
      const int attr = mesh.attributes[i];
      const double eps = eps_by_attr(attr - 1);
      if (!(std::isfinite(eps) && eps > 0.0))
      {
        bad_attr = attr;
        break;
      }
    }
    MFEM_VERIFY(bad_attr < 0,
                "Missing or invalid permittivity for mesh volume attribute "
                << bad_attr << ". Ensure materials.*.attr_id covers all mesh attributes.");

    // ctor available in your MFEM: PWConstCoefficient(Vector &c)
    return PWConstCoefficient(eps_by_attr);
}

std::unique_ptr<mfem::ParGridFunction> SolvePoisson(ParFiniteElementSpace &pfes,
                                                    const BoundaryConditionGroups BCs,
                                                    const std::shared_ptr<const Config>& cfg,
                                                    std::string dir_overwrite)
{
  using namespace mfem;

  if (cfg->debug.debug) {std::cout << "[DEBUG] In Poisson Solver" << std::endl;}
  MPI_Comm comm = pfes.GetParMesh()->GetComm();

  const std::filesystem::path directory = dir_overwrite.empty() ? cfg->save_path : dir_overwrite;
  const std::filesystem::path log_path = directory / "solver.log";

  const Mesh &mesh = *pfes.GetMesh();

  PWConstCoefficient epsilon_pw = BuildEpsilonPWConst(mesh, cfg);
  auto w = MakeAxisymWeightCoeff(cfg->solver.axisymmetric, 0);
  mfem::ProductCoefficient weps(*w, epsilon_pw);
  
  std::unique_ptr<ParGridFunction> V;
  std::unique_ptr<ParBilinearForm> a;
  std::unique_ptr<ParLinearForm>   b;
  OperatorHandle                   A;
  Vector                        X, B;
  std::function<std::unique_ptr<Solver>(OperatorHandle&)> make_prec;

  V = std::make_unique<ParGridFunction>(&pfes);
  a = std::make_unique<ParBilinearForm>(&pfes);
  b = std::make_unique<ParLinearForm>(&pfes);
  *V = 0.0;

  

  // Apply BC Markers
  ApplyDirichletValues(*V, BCs.dirichlet_attr, cfg);
  
  a->AddDomainIntegrator(new DiffusionIntegrator(weps));
  a->Assemble();       

  std::vector<std::unique_ptr<mfem::Coefficient>> neumann_coeffs; // Required for functional bcs to not go out of scope
  std::vector<mfem::Array<int>> neumann_markers;
  if (BCs.has_neumann()) { ApplyNeumannValues(*b, BCs.neumann_attr, cfg, *w, neumann_coeffs, neumann_markers); }         

  b->Assemble();

  Array<int> ess_tdof;
  pfes.GetEssentialTrueDofs(BCs.dirichlet_attr, ess_tdof);

  a->FormLinearSystem(ess_tdof, *V, *b, A, X, B);  
  auto *Ap = A.As<mfem::HypreParMatrix>();
  MFEM_VERIFY(Ap != nullptr, "FormLinearSystem did not produce a HypreParMatrix.");

  MFEM_VERIFY(cfg->solver.solver == "MUMPS" || cfg->solver.solver == "CG",
              "Unknown solver '" << cfg->solver.solver
              << "'. Supported values are 'MUMPS' and 'CG'.");

  if (cfg->solver.solver == "MUMPS")
  {
    // TODO Add warning if no dirichlet is supplied the Sym Type is SYMMETRIC_INDEFINITE
    if (cfg->debug.debug) std::cout << "[DEBUG] Using MUMPS solver" << std::endl;
    mfem::MUMPSSolver mumps(Ap->GetComm());
    mumps.SetPrintLevel(cfg->solver.printlevel); // must be before SetOperator
    mumps.SetReorderingReuse(true); // TODO Cmd line flag
    mumps.SetReorderingStrategy(mfem::MUMPSSolver::AUTOMATIC); // TODO Cmd line flag
    mumps.SetMatrixSymType(
        mfem::MUMPSSolver::MatType::SYMMETRIC_POSITIVE_DEFINITE);
    std::chrono::steady_clock::time_point t_start;
    if (cfg->debug.timing) t_start = std::chrono::steady_clock::now();
    mumps.SetOperator(*Ap);  // factorization
    mumps.Mult(B, X);        // solve
    if (cfg->debug.timing)
    {
        auto t_end = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
        std::cout << "[Timing]: MUMPS Factorization + Solve (" << dt << " ms)" << std::endl;
    }
    mumps.SetPrintLevel(cfg->solver.printlevel);
  }
  else if (cfg->solver.solver == "CG")
  {
    if (cfg->debug.debug) std::cout << "[DEBUG] Using CG solver" << std::endl;
    // Build AMG preconditioner directly
    auto P = std::make_unique<mfem::HypreBoomerAMG>(*Ap);
    P->SetPrintLevel(cfg->solver.printlevel);

    {
        HYPRE_Solver hsolver = (HYPRE_Solver)(*P);
        HYPRE_BoomerAMGSetPrintFileName(hsolver, log_path.c_str());
    }

    if (auto *hypre_prec = dynamic_cast<mfem::HypreSolver*>(P.get()))
    {
        hypre_prec->SetErrorMode(
            cfg->debug.printHypreWarnings
                ? mfem::HypreSolver::WARN_HYPRE_ERRORS
                : mfem::HypreSolver::IGNORE_HYPRE_ERRORS);
    }

    mfem::CGSolver cg(comm);
    cg.SetOperator(*A.Ptr());
    cg.SetPreconditioner(*P);
    cg.SetRelTol(cfg->solver.rtol);
    cg.SetAbsTol(cfg->solver.atol);
    cg.SetMaxIter(cfg->solver.maxiter);
    cg.SetPrintLevel(cfg->solver.printlevel);

    std::ofstream it_log(log_path);
    ResidualFileMonitor monitor(it_log);
    cg.SetMonitor(monitor);
    std::chrono::steady_clock::time_point t_start;
    if (cfg->debug.timing) t_start = std::chrono::steady_clock::now();
    cg.Mult(B, X);
    if (cfg->debug.timing)
    {
        auto t_end = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();
        std::cout << "[Timing]: CG + AMG Solve (" << dt << " ms)" << std::endl;
    }
  }
  else
  {
    MFEM_ABORT("Unhandled solver branch.");
  }

  // As before:
  a->RecoverFEMSolution(X, *b, *V);


  return V;
}
