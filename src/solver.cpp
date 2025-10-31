#include "solver.h"


// Build ε(x) that is piecewise-constant over element attributes (volume tags)
static PWConstCoefficient BuildEpsilonPWConst(const Mesh &mesh, const std::shared_ptr<const Config>& cfg)
{
    // attributes are 1-based; we need a vector sized by the max attribute id
    const int max_attr = mesh.attributes.Max();  // e.g. 2004 in your case
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
      if (mat.id > 0)  set_eps(mat.id, mat.epsilon_r);
      else if (name == "Default")
      {
        // Fill in any remaining attributes that were not explicitly set
        for (int a = 1; a <= max_attr; ++a)
        {
          // TODO Verify the logic + check if we want to actually do this 
          if (eps_by_attr(a - 1) == 0.0)  eps_by_attr(a - 1) = mat.epsilon_r;
        }
      }
    }
    // ctor available in your MFEM: PWConstCoefficient(Vector &c)
    return PWConstCoefficient(eps_by_attr);
}

GridFunction SolvePoisson(FiniteElementSpace &fespace, const Array<int> &dirichlet_attr, const std::shared_ptr<const Config>& cfg)
{
    GridFunction V(&fespace);
    
    V = 0.0;
    ApplyDirichletValues(V, dirichlet_attr, cfg);

    BilinearForm a(&fespace);

    // ---- Add Material Properties -------
    const Mesh &mesh = *fespace.GetMesh();
    PWConstCoefficient epsilon_pw = BuildEpsilonPWConst(mesh, cfg);
    a.AddDomainIntegrator(new DiffusionIntegrator(epsilon_pw));
    a.Assemble();
    a.Finalize();

    LinearForm b(&fespace);
    //Charge density goes here if required
    b.Assemble();

    Array<int> ess_tdof_list;
    fespace.GetEssentialTrueDofs(dirichlet_attr, ess_tdof_list);

    SparseMatrix A;
    Vector B, X;
    a.FormLinearSystem(ess_tdof_list, V, b, A, X, B);

    // Solve with CG
    CGSolver cg;
    cg.SetRelTol(cfg->solver.rtol);
    cg.SetAbsTol(cfg->solver.atol);
    cg.SetMaxIter(cfg->solver.maxiter);
    cg.SetPrintLevel(cfg->solver.printlevel);

    DSmoother prec(A);
    cg.SetPreconditioner(prec);
    cg.SetOperator(A);
    cg.Mult(B, X);

    a.RecoverFEMSolution(X, b, V);

    return V;   // safe: fespace alive in main
}
