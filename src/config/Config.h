#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <limits>
#include <mpi.h>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <cstring>

// -------------------- Mesh Specific ----------------------------
struct Boundary {
    int bdr_id = -1;        // bdr_id
    std::string type;       // "dirichlet" | "neumann" | "neumann_internal" | "robin"
    double value = std::numeric_limits<double>::quiet_NaN();
    // Used for depth dependent neumann only so far
    bool depth_dependent = false;
    double z_bot = 0.0;
    double z_top = 0.0;
    double value_bot = 0.0;
    double value_top = 0.0;
};

struct Material {
    int id = -1;            // attr_id
    double epsilon_r = std::numeric_limits<double>::quiet_NaN();
};
struct TMOPSettings {
    bool enable = false;
    int max_iter = 5;
    int metric_id = 2;
    std::string target_type = "IdealShapeUnitSize";
    std::string optimizer = "Newton";
    double step_limit = 0.1;
    double rel_tol = 1e-6;
    double abs_tol = 1e-12;
    bool verbose = false;
};
struct AMRSettings
{
    // ---- Core control ----
    bool enable = true;          // Master AMR switch
    int  max_iter = 10;           // Max adaptive solve iterations

    // ---- Refinement tolerance ----
    double local_error_goal = 1e-3;  // Stop when all element errors <= this

    // If > 0, additionally limit by global total error (p-norm)
    // Leave 0.0 to ignore (purely local goal driven)
    double total_error_goal = 0.0;

    // Only used if you want total-error-fraction based marking.
    // Set to 0.0 for pure local-threshold mode.
    double total_error_fraction = 0.0;

    // p-norm used to compute total error (2.0 = L2 norm)
    double total_error_norm_p = 2.0;

    // ---- Derefinement control ----
    bool enable_derefine = false;

    // Derefine threshold = hysteresis * local_error_goal
    // Should be < 1.0 to avoid oscillation (typical 0.3 – 0.7)
    double derefine_hysteresis = 0.5;

    // Combine children error when testing derefinement:
    // 0 = min, 1 = sum, 2 = max  (matches MFEM ThresholdDerefiner::SetOp)
    int derefine_op = 2;

    // ---- Mesh safety limits ----
    long long max_elements = 0;   // 0 = unlimited
    long long max_dofs = 0;       // 0 = unlimited (user-enforced check)

    // Limit nonconforming refinement level difference (0 = unlimited)
    int nc_limit = 0;

    // Prefer conforming refinement (true for simplices)
    // For quad/hex local AMR typically false (prefer nonconforming)
    bool prefer_conforming_refinement = true;

    // ---- Early termination heuristics ----

    // Stop if fewer than this many elements were marked in a refine step
    int min_marked_elements = 1;

    // Stop if relative total error reduction between iterations
    // is smaller than this threshold (e.g. 1e-3)
    double min_rel_error_reduction = 0.0;

    // ---- Estimator selection ----
    enum class EstimatorType
    {
        Kelly,              // KellyErrorEstimator
        ZienkiewiczZhu,     // ZienkiewiczZhuEstimator
        L2ZienkiewiczZhu    // L2ZZ (smoother, slightly more expensive)
    };

    EstimatorType estimator = EstimatorType::Kelly;

    // ---- Parallel-specific behavior ----

    // Rebalance ParMesh after refinement/derefine
    bool enable_rebalance = true;

    // Print AMR diagnostics (per rank 0 typically)
    bool verbose = false;

    // ---- AMR mesh export outputs ----
    // Serial MFEM mesh: amr_mesh_serial.mesh
    bool save_serial = false;
    // Parallel VTU/PVTU: amr_mesh_vtu.pvtu + amr_mesh_vtu_*.vtu
    bool save_vtu_parallel = false;
};


struct MeshSettings {
    std::string path = ""; // TODO Does mesh need more settings?
    int AMR_TMOP_iter = 1;
    AMRSettings amr;
    TMOPSettings tmop;
};

// -------------------- Compute / Runtime Settings ----------------------------
struct MPISettings {
    bool enabled = true;
    int  ranks = 1;        
    bool repartition_after_refine = true;
};

struct ThreadsSettings {
    bool enabled = false;
    bool num_auto = true;       // true if "auto" was given TODO is this used
    int  num = 0;               // 0 means "auto" when num_auto=true
    std::string affinity = "compact"; // "compact" | "scatter" | "none"
};

struct DeviceRuntime {
    std::string type = "none";  // "none" | "cuda" | "hip" | "occa" | "cpu"
    bool id_auto = true;        // true if "auto"
    int  id = 0;                // ignored if id_auto=true
    int  per_rank = 1;
};

struct ComputeSettings {
    MPISettings     mpi;
    ThreadsSettings threads;
    DeviceRuntime   device;
};

// If debug enabled but nothing else we default to maximal debug
struct DebugSettings
{
    bool debug                   = false;
    bool timing                  = false;
    bool dry_run                 = false; // TODO Remove? 
    bool printBoundaryConditions = false;
    bool printHypreWarnings      = false;

    // Dump data to file where applicable
    bool dumpdata              = false;
};

// Voltage assignment / resistor-chain behavior
struct VoltageSettings
{
    // If true, fail when a resistor-network connected component has no fixed node.
    // This catches floating electrodes after field-cage solve.
    bool error_on_floating_after_resistor_chain = true;
};

// -------------------- Circuit Computation --------------------------
struct FieldCageNode {
    std::string name;      // Name to reference by 
    std::string boundary;  // Associated Boundary condition
    bool fixed = false;    // true if potential is fixed by the boundary condition (ie cathode & top field shaping)
};

struct FieldCageEdge { 
    std::string n1;      // node name from
    std::string n2;      // node name to
    std::string R_name;  // resistance from n1 to n2 
};

struct FieldCageNetworkSettings {
    bool enabled = false;
    std::vector<FieldCageNode> nodes; // BC 
    std::vector<FieldCageEdge> edges; // Resistors between two nodes
    std::unordered_map<std::string, double> R_values; // Named resistors 
};


// -------------------- Sweep description ----------------------------
struct Assignment {
    std::string path;   // dot-path into YAML
    std::string value;  // stored as string; parsed later with your parse_scalar
};

struct FixedConfig {
    std::string label;                 // optional, but useful for metadata
    std::vector<Assignment> assigns;   // multiple path/value pairs
};

struct SweepEntry { 
    enum class Kind { Discrete, Range, Fixed };

    std::string name;   // optional label
    std::string path;   // dot-path into YAML, e.g. "boundaries.name.value"

    Kind kind = Kind::Discrete;

    std::vector<std::string> values; // Discrete interface

    // Range interface
    double start = 0.0;
    double end   = 0.0;
    int    steps = 0;   // inclusive

    // FIxed configs
    std::vector<FixedConfig> configs;
};

// ----------------- Optimization description ------------------------
// Currently no support for discrete variables implemented
struct OptimizeVar {
    std::string name;   // label
    std::string path;   // dot path into Config, like "solver.order" or "boundaries.BC_Anode.value"

    // For continuous (bounded) variables:
    double lower = 0.0;
    double upper = 0.0;
    double initial = 0.0;       // starting guess; if 0, you can derive from lower/upper

    // For discrete variables:
    std::vector<std::string> values;  // a small, finite set of allowed values
};
struct OptimizationSettings {
    bool enabled = false;
    bool metrics_only = false; // Triggers no mesh simulation but computes metrics for all available simulations
    bool print_results = false;

    // Optimization target
    std::string objective;

    // Evaluation domain
    double r_min          = 0.0;
    double r_max          = 0.664;
    double z_min          = -1.5028;
    double z_max          = 0.004;

    // Variables to tune:
    std::vector<OptimizeVar> variables;

    // Nelder-Mead Settings
    int    max_iters      = 200;
    int    max_fun_evals  = 200;
    double tol_fun        = 1e-3;
    double tol_x          = 1e-3;
    bool   adaptive       = true;
};

struct CIVSettings {
    std::string method = "ColumnSweep";
    // Random Sample config
    int num_seed_elements = 512;
    // For grid searches
    int nr = 100;
    int nz = 12;
    // For adaptive grid
    int max_levels = 5;
    // For row scanning 
    int block_size = 64;
    // ---------- Not Exposed
    // Integration order
    int    ir_order       = 1;
    // Random Sampling seed - Expose?
    int    rng_seed       = 67; 
};

struct FieldSpreadParams {
    double lower = 0.5;
    double upper = 0.95;
};

// -------------------- Compute / Runtime Settings ----------------------------
struct SolverSettings {
    // Postprocessing controls
    bool generate_E = true;
    // MFEM / solve controls
    bool axisymmetric = false;
    int order = 3;
    std::string assembly_mode = "partial";
    
    std::string solver = "MUMPS";
    
    // MUMPS Settings
    // When we get to 3D need to tune out of core memory relaxation etcs

    // CG Settings
    double atol = 1.0;
    double rtol = 0.0;
    int    maxiter = 100000;
    
    int    printlevel = 1;
};
// Field Line tracing parameters 
struct ElectronTraceParams
{
    std::string provider  = "VTK";
    std::string method    = "RK34";
    // Relative to the smallest mesh element 
    double c_step         = 2;
    // Tolerance for r,z boundary checks (axis clamp, z-range, etc.)
    double geom_tol       = 1e-12;
    // For MPI 
    int redistribution_every = 1;

    bool use_l2_electric_field = true;

    // tracing region
    double r_min = 0.0;
    double r_max = 0.664;
    double z_min = -1.5028;
    double z_max = 0.004;
    // Overwrite for z_max to speed up tracing
    double tracing_z_max;

    // Steps to let electrons traverse n times, if 0 infinite loop
    double max_traversals = 10;
};
// ------------------------- Interpolation --------------
enum class InterpolationCode1Mode
{
  NaN = 0,
  AcceptValue = 1,
  AverageElements = 2
};

struct Interpolate
{
  int Nx = 1;
  int Ny = 1;
  int Nz = 1;
  double x_min = std::numeric_limits<double>::quiet_NaN();
  double x_max = std::numeric_limits<double>::quiet_NaN();
  double y_min = std::numeric_limits<double>::quiet_NaN();
  double y_max = std::numeric_limits<double>::quiet_NaN();
  double z_min = std::numeric_limits<double>::quiet_NaN();
  double z_max = std::numeric_limits<double>::quiet_NaN();
  bool H1_project = false;
  bool E = true;
  bool V = false;
  InterpolationCode1Mode code1_mode = InterpolationCode1Mode::AverageElements;
  // Not Exposed
  bool accept_surface_projection = false;
  // TODO Integrate the below, requires knowledge of mesh dimensions
  double res_x = 0;
  double res_y = 0;
  double res_z = 0;

};
// ------------------------- Actual Config Struct -------------------------------
struct Config {
    int schema_version = 1;
    std::string geometry_id;

    std::string save_path; 
    bool delete_files_present = false;

    MeshSettings mesh;
    ComputeSettings compute;
    DebugSettings  debug;
    VoltageSettings voltage;
    SolverSettings solver;

    std::unordered_map<std::string, Boundary> boundaries;
    std::unordered_map<std::string, Material> materials;

    // Sweep Config options
    std::vector<SweepEntry> sweeps;

    // Optimization Config Values
    OptimizationSettings optimize;

    // Configuration to solve circuits
    FieldCageNetworkSettings fieldcage_network;

    // Tracing Parameters
    ElectronTraceParams tracing_params;

    // CIV Computation parameters
    CIVSettings civ_params;

    // FieldSpread Parameters
    FieldSpreadParams  field_spread;

    // Interpolation settings
    Interpolate interp;

    // Internal for runtime config checking
    std::string run_mode = "sim";

    static Config Load(const std::string& path);
    static Config Load(const std::string& path, std::string run_mode);

    static Config LoadFromString(const std::string& yaml_str);
    static Config LoadFromString(const std::string& yaml_str, std::string run_mode);

    static Config LoadFromNode(const YAML::Node& root);
    static Config LoadFromNode(const YAML::Node& root, std::string run_mode);
};
std::string ReadConfigString(const std::string& path, MPI_Comm comm);



void apply_fieldcage_network(YAML::Node &root);
void apply_fieldcage_network(Config &cfg);
