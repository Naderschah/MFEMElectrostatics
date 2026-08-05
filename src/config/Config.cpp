// src/config/Config.cpp
#include "Config.h"
#include <stdexcept>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <unordered_set>
#include <mpi.h>





// ------ Print formating TODO Standardize across all modules
inline bool use_color() {
    const char* no = std::getenv("NO_COLOR");
    if (no && *no) return false;
    return true;
}
inline void Warning(const std::string& msg) {
    if (use_color()) {
        std::cerr << "\033[33mWarning:\033[0m " << msg << '\n'; // yellow
    } else {
        std::cerr << "Warning: " << msg << '\n';
    }
}
[[noreturn]] inline void Error(const std::string& msg) {
    if (use_color()) {
        std::cerr << "\033[31mError:\033[0m " << msg << '\n'; // red
    } else {
        std::cerr << "Error: " << msg << '\n';
    }
    throw std::runtime_error(msg);
}


// read int or "auto"
static std::pair<int,bool> read_int_or_auto(const YAML::Node& n, int dflt_val, bool dflt_auto=true)
{
    if (!n) return {dflt_val, dflt_auto};
    if (n.IsScalar()) {
        const std::string s = n.as<std::string>();
        if (s == "auto" || s == "AUTO" || s == "Auto") return {dflt_val, true};
        try {
            int v = std::stoi(s);
            return {v, false};
        } catch (...) {
            // If scalar but not parseable, fall back to defaults
            return {dflt_val, dflt_auto};
        }
    }
    if (n.IsSequence() || n.IsMap()) {
        // unexpected type; keep defaults
        return {dflt_val, dflt_auto};
    }
    // Try as int directly
    try { return { n.as<int>(), false }; }
    catch (...) { return { dflt_val, dflt_auto }; }
}

// parse compute.* blocks
static void parse_compute(Config &cfg, const YAML::Node& root)
{
    if (root["compute"]) {
        const auto C = root["compute"];

        // --- mpi ---
        if (C["mpi"]) {
            const auto M = C["mpi"];

            cfg.compute.mpi.enabled =
                M["enabled"].as<bool>(cfg.compute.mpi.enabled);

            cfg.compute.mpi.repartition_after_refine =
                M["repartition_after_refine"].as<bool>(
                    cfg.compute.mpi.repartition_after_refine);
        }

        // --- threads ---
        if (C["threads"]) {
            const auto T = C["threads"];

            cfg.compute.threads.enabled =
                T["enabled"].as<bool>(cfg.compute.threads.enabled);

            auto [tnum, t_auto] =
                read_int_or_auto(T["num"],
                                 cfg.compute.threads.num,
                                 cfg.compute.threads.num_auto);
            cfg.compute.threads.num      = tnum;
            cfg.compute.threads.num_auto = t_auto;

            cfg.compute.threads.affinity =
                T["affinity"].as<std::string>(cfg.compute.threads.affinity);
        }

        // --- device ---
        if (C["device"]) {
            const auto D = C["device"];

            cfg.compute.device.type =
                D["type"].as<std::string>(cfg.compute.device.type);

            auto [did, d_auto] =
                read_int_or_auto(D["id"],
                                 cfg.compute.device.id,
                                 cfg.compute.device.id_auto);
            cfg.compute.device.id      = did;
            cfg.compute.device.id_auto = d_auto;

            cfg.compute.device.per_rank =
                D["per_rank"].as<int>(cfg.compute.device.per_rank);
        }
    }

    // Convenience: if a GPU-like device is selected and no assembly_mode set → partial.
    // (Assumes "none" means "no GPU"; adjust if you add more device types.)
    if (cfg.compute.device.type != "none" && cfg.solver.assembly_mode.empty()) {
        cfg.solver.assembly_mode = "partial";
    }

    // If threads disabled → force single-thread behavior
    if (!cfg.compute.threads.enabled) {
        cfg.compute.threads.num_auto = false;
        if (cfg.compute.threads.num <= 0) {
            cfg.compute.threads.num = 1;
        }
    }
}


static SweepEntry::Kind parse_sweep_kind(const std::string &s_raw)
{
    std::string s = s_raw;
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (s == "discrete" || s.empty()) return SweepEntry::Kind::Discrete;
    if (s == "range")                 return SweepEntry::Kind::Range;
    if (s == "fixed" || s == "preset" || s == "presets") return SweepEntry::Kind::Fixed;

    std::cerr << "Warning: unknown sweep kind \"" << s_raw
              << "\"; defaulting to Discrete.\n";
    return SweepEntry::Kind::Discrete;
}
static std::string node_to_string(const YAML::Node &n)
{
    if (!n) return std::string{};

    // If it's already a scalar, try reading as string (works for "1", "1.2", "true", etc.)
    if (n.IsScalar()) {
        return n.as<std::string>();
    }

    // For non-scalars, emit to YAML one-liner. (Allows e.g. small sequences if you ever want them.)
    YAML::Emitter out;
    out << n;
    return std::string(out.c_str());
}
static void parse_sweeps(Config &cfg, const YAML::Node &root)
{
    cfg.sweeps.clear();

    if (!root["sweeps"]) return;

    const YAML::Node S = root["sweeps"];
    if (!S.IsSequence()) {
        std::cerr << "Warning: 'sweeps' must be a sequence in YAML.\n";
        return;
    }

    for (std::size_t i = 0; i < S.size(); ++i) {
        const YAML::Node &node = S[i];
        if (!node || !node.IsMap()) {
            std::cerr << "Warning: sweeps[" << i << "] is not a map; skipping.\n";
            continue;
        }

        SweepEntry sw;

        sw.name = node["name"].as<std::string>(std::string{});

        std::string kind_str = node["kind"].as<std::string>(std::string{"discrete"});
        sw.kind = parse_sweep_kind(kind_str);

        // Path is required only for discrete/range
        if ((sw.kind == SweepEntry::Kind::Discrete) || (sw.kind == SweepEntry::Kind::Range))
        {
        sw.path = node["path"].as<std::string>(std::string{});
        if ((sw.kind == SweepEntry::Kind::Discrete || sw.kind == SweepEntry::Kind::Range) && sw.path.empty()) {
            std::cerr << "Warning: sweeps[" << i << "] missing 'path'; skipping.\n";
            continue;
        }}

        // -------- Discrete --------
        if (sw.kind == SweepEntry::Kind::Discrete) {
            if (!node["values"]) {
                std::cerr << "Warning: sweeps[" << i
                          << "] kind=discrete but no 'values' provided; skipping.\n";
                continue;
            }
            const YAML::Node &vals = node["values"];
            if (!vals.IsSequence()) {
                std::cerr << "Warning: sweeps[" << i
                          << "].values is not a sequence; skipping entry.\n";
                continue;
            }

            sw.values.clear();
            sw.values.reserve(vals.size());
            for (std::size_t j = 0; j < vals.size(); ++j) {
                sw.values.push_back(node_to_string(vals[j]));
            }
            if (sw.values.empty()) {
                std::cerr << "Warning: sweeps[" << i
                          << "] has empty 'values' for discrete kind; skipping.\n";
                continue;
            }
        }

        // -------- Range --------
        if (sw.kind == SweepEntry::Kind::Range) {
            sw.start = node["start"].as<double>(sw.start);
            sw.end   = node["end"].as<double>(sw.end);
            sw.steps = node["steps"].as<int>(sw.steps);

            if (sw.steps <= 0) {
                std::cerr << "Warning: sweeps[" << i
                          << "] kind=range has non-positive 'steps'; skipping.\n";
                continue;
            }
        }

        // -------- Fixed --------
        if (sw.kind == SweepEntry::Kind::Fixed) {
            if (!node["configs"] || !node["configs"].IsSequence()) {
                std::cerr << "Warning: sweeps[" << i
                          << "] kind=fixed requires 'configs' (a sequence); skipping.\n";
                continue;
            }

            const YAML::Node &cfgs = node["configs"];
            sw.configs.clear();
            sw.configs.reserve(cfgs.size());

            for (std::size_t ci = 0; ci < cfgs.size(); ++ci) {
                const YAML::Node &cnode = cfgs[ci];
                if (!cnode || !cnode.IsMap()) {
                    std::cerr << "Warning: sweeps[" << i << "].configs[" << ci
                              << "] not a map; skipping that config.\n";
                    continue;
                }

                FixedConfig fc;
                fc.label = cnode["label"].as<std::string>(std::string{});
                if (fc.label.empty()) {
                    // provide a stable default label if user omitted it
                    fc.label = "cfg_" + std::to_string(ci);
                }

                if (!cnode["set"] || !cnode["set"].IsMap()) {
                    std::cerr << "Warning: sweeps[" << i << "].configs[" << ci
                              << "] requires 'set' (a map of dotpath: value); skipping that config.\n";
                    continue;
                }

                const YAML::Node &set = cnode["set"];
                for (auto it = set.begin(); it != set.end(); ++it) {
                    const std::string p = it->first.as<std::string>();
                    const YAML::Node  v = it->second;
                    if (p.empty()) {
                        std::cerr << "Warning: sweeps[" << i << "].configs[" << ci
                                  << "] has empty key in 'set'; skipping that entry.\n";
                        continue;
                    }
                    fc.assigns.push_back({p, node_to_string(v)});
                }

                if (fc.assigns.empty()) {
                    std::cerr << "Warning: sweeps[" << i << "].configs[" << ci
                              << "] has empty 'set'; skipping that config.\n";
                    continue;
                }

                sw.configs.push_back(std::move(fc));
            }

            if (sw.configs.empty()) {
                std::cerr << "Warning: sweeps[" << i
                          << "] kind=fixed has no valid configs; skipping.\n";
                continue;
            }
        }

        cfg.sweeps.push_back(std::move(sw));
    }
}

static void parse_fieldcage_network(Config &cfg, const YAML::Node &root)
{
    if (!root["fieldcage_network"]) return;
    const auto FC = root["fieldcage_network"];

    cfg.fieldcage_network.enabled =
        FC["enabled"].as<bool>(cfg.fieldcage_network.enabled);

    if (!cfg.fieldcage_network.enabled) return;

    // --- nodes ---
    if (FC["nodes"]) {
        for (const auto &nd : FC["nodes"]) {
            FieldCageNode node;
            node.name     = nd["name"].as<std::string>();
            node.boundary = nd["boundary"].as<std::string>("");
            node.fixed    = nd["fixed"].as<bool>(false);
            cfg.fieldcage_network.nodes.push_back(std::move(node));
        }
    }

    // --- resistor values ---
    if (FC["resistors"]) {
        const auto Rnode = FC["resistors"];
        if (!Rnode.IsMap()) {
            throw std::runtime_error("fieldcage_network.resistors must be a mapping of name -> value");
        }
        for (auto it : Rnode) {
            std::string rname = it.first.as<std::string>();
            double rval       = it.second.as<double>();
            cfg.fieldcage_network.R_values[rname] = rval;
        }
    }

    // --- edges ---
    if (FC["edges"]) {
        for (const auto &ed : FC["edges"]) {
            FieldCageEdge e;
            e.n1     = ed["n1"].as<std::string>();
            e.n2     = ed["n2"].as<std::string>();
            e.R_name = ed["R"].as<std::string>();
            cfg.fieldcage_network.edges.push_back(std::move(e));
        }
    }
}
static void parse_optimization(Config &cfg, const YAML::Node &root)
{
    if (!root["optimize"]) {
        return; // not required
    }

    const YAML::Node O = root["optimize"];

    cfg.optimize.enabled =
        O["enabled"].as<bool>(cfg.optimize.enabled);

    cfg.optimize.metrics_only = O["metrics_only"].as<bool>(cfg.optimize.metrics_only);
    
    cfg.optimize.print_results =
        O["print_results"].as<bool>(cfg.optimize.print_results);

    // Domain bounds
    cfg.optimize.r_min = O["r_min"].as<double>(cfg.optimize.r_min);
    cfg.optimize.r_max = O["r_max"].as<double>(cfg.optimize.r_max);
    cfg.optimize.z_min = O["z_min"].as<double>(cfg.optimize.z_min);
    cfg.optimize.z_max = O["z_max"].as<double>(cfg.optimize.z_max);

    // Objective name (string keyword)
    cfg.optimize.objective =
        O["objective"].as<std::string>(cfg.optimize.objective);

    // NM controls (names mapped from your YAML)
    cfg.optimize.max_fun_evals =
        O["max_evals"].as<int>(cfg.optimize.max_fun_evals);
    cfg.optimize.max_iters =
        O["max_iters"].as<int>(cfg.optimize.max_iters);
    cfg.optimize.tol_fun =
        O["rel_tol_f"].as<double>(cfg.optimize.tol_fun);
    cfg.optimize.tol_x =
        O["rel_tol_x"].as<double>(cfg.optimize.tol_x);
    cfg.optimize.adaptive =
        O["adaptive"].as<bool>(cfg.optimize.adaptive);

    // Variables
    if (O["variables"]) {
        for (const auto &vnode : O["variables"]) {
            OptimizeVar var;

            var.name = vnode["name"].as<std::string>("");
            var.path = vnode["path"].as<std::string>();

            if (!vnode["lower"] || !vnode["upper"]) {
                throw std::runtime_error(
                    "Optimization variable '" +
                    (var.name.empty() ? var.path : var.name) +
                    "' must have 'lower' and 'upper'");
            }

            var.lower   = vnode["lower"].as<double>();
            var.upper   = vnode["upper"].as<double>();

            if (vnode["initial"]) {
                var.initial = vnode["initial"].as<double>();
            } else {
                // default to midpoint if no initial provided
                var.initial = 0.5 * (var.lower + var.upper);
            }

            cfg.optimize.variables.push_back(std::move(var));
        }
    }
}

static void parse_fieldspread(Config &cfg, const YAML::Node &root)
{
    if (!root["fieldSpread_params"]) {
        return; // not required TODO Handle
    }

    const YAML::Node O = root["fieldSpread_params"];

    cfg.field_spread.lower = O["BottomPercentile"].as<double>(cfg.field_spread.lower);
    cfg.field_spread.upper = O["UpperPercentile"].as<double>(cfg.field_spread.upper);
}

// -----------------------------------------------------------------------------
// Trace params
// -----------------------------------------------------------------------------
static void parse_trace_params(Config &cfg, const YAML::Node &root)
{
    // Start from current config values
    ElectronTraceParams params;

    const auto opt = root["optimize"];
    if (!opt) {
        return;
    }
    const auto O = opt["trace_params"];
    if (!O) {
        return;
    }
    params.provider = O["provider"].as<std::string>(cfg.tracing_params.provider);
    params.method = O["method"].as<std::string>(cfg.tracing_params.method);

    params.c_step = O["c_step"].as<double>(cfg.tracing_params.c_step);
    params.geom_tol = O["geom_tol"].as<double>(cfg.tracing_params.geom_tol);

    params.r_min = O["r_min"].as<double>(cfg.optimize.r_min);
    params.r_max = O["r_max"].as<double>(cfg.optimize.r_max);
    params.z_min = O["z_min"].as<double>(cfg.optimize.z_min);
    params.z_max = O["z_max"].as<double>(cfg.optimize.z_max);
    params.tracing_z_max = O["tracing_z_max"].as<double>(params.z_max);

    params.max_traversals = O["max_traversals"].as<double>(cfg.tracing_params.max_traversals);

    params.redistribution_every = O["redistribution_every"].as<int>(cfg.tracing_params.redistribution_every);

    if (params.method != "Euler-Cauchy" &&
        params.method != "RK4" &&
        params.method != "RK54" &&
        params.method != "RK45") 
    { Error("trace_params.method must be one of {Euler-Cauchy, RK4, RK45, RK54}"); }
    if (params.c_step <= 0) { Error("trace_params.c_step parameter must be larger than 0"); }
    if (params.geom_tol <= 0) { Error("trace_params.geom_tol parameter must be larger than 0"); }
    if (params.r_min >= params.r_max) { Error("trace_params.r_min must be smaller than trace_params.r_max"); }
    if (params.z_min >= params.z_max) { Error("trace_params.z_min must be smaller than trace_params.z_max"); }
    if (params.max_traversals < 0) { Error("trace_params.max_traversals parameter must at least 0 (0 being no limit)"); }
    if (params.provider != "VTK" &&
        params.provider != "MPITracer" &&
        params.provider != "BOOST") 
    { Error("trace_params.provider must be one of {VTK, BOOST}"); }
    cfg.tracing_params = params;

    if ((params.provider == "MPITracer") && (params.method != "Euler-Cauchy") && (params.method != "RK4"))
        Error("MPITracer supports Euler-Cauchy and RK4 only");
    
    int world_size = 0;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    if ((params.provider == "MPITracer") && (world_size == 1))
        Warning("MPITracer Running Effectively single threaded, use mpirun -np n as command prefix with n the number of processes to use");
}

// ---------------------- CIV Params 
static void parse_civ_params(Config &cfg, const YAML::Node &root)
{
    if (!root["optimize"]) {
        return; // not required
    }
    if (!root["optimize"]["civ_params"]) {
        return; // not required
    }

    const YAML::Node O = root["optimize"]["civ_params"];

    cfg.civ_params.method = O["method"].as<std::string>(cfg.civ_params.method);
    cfg.civ_params.num_seed_elements = O["num_seed_elements"].as<int>(cfg.civ_params.num_seed_elements);
    cfg.civ_params.nr = O["nr"].as<int>(cfg.civ_params.nr);
    cfg.civ_params.nz = O["nz"].as<int>(cfg.civ_params.nz);
    cfg.civ_params.max_levels = O["max_levels"].as<int>(cfg.civ_params.max_levels);
    cfg.civ_params.block_size = O["block_size"].as<int>(cfg.civ_params.block_size);
}

static InterpolationCode1Mode parse_interp_code1_mode(const YAML::Node &node,
                                                      const InterpolationCode1Mode current_mode)
{
    if (!node)
    {
        return current_mode;
    }

    if (!node.IsScalar())
    {
        throw std::runtime_error("interpolate.code1_mode must be a scalar.");
    }

    std::string mode = node.as<std::string>("");
    std::transform(mode.begin(), mode.end(), mode.begin(),
                   [](unsigned char c) -> char
                   {
                       if (c == '-' || c == ' ') { return '_'; }
                       return static_cast<char>(std::tolower(c));
                   });

    if (mode == "0" || mode == "nan")
    {
        return InterpolationCode1Mode::NaN;
    }
    if (mode == "1" || mode == "accept" || mode == "accept_value")
    {
        return InterpolationCode1Mode::AcceptValue;
    }
    if (mode == "2" || mode == "average" || mode == "average_elements")
    {
        return InterpolationCode1Mode::AverageElements;
    }

    throw std::runtime_error("interpolate.code1_mode must be one of {nan|0, accept_value|1, average_elements|2}.");
}

static void parse_interp_params(Config &cfg, const YAML::Node &root)
{
    if (!root["interpolate"]) {
        return; // not required
    }

    const YAML::Node O = root["interpolate"];

    // cfg.interp.res_x = O["res_x"].as<double>(cfg.interp.res_x);
    // cfg.interp.res_y = O["res_y"].as<double>(cfg.interp.res_y);
    // cfg.interp.res_z = O["res_z"].as<double>(cfg.interp.res_z);
    cfg.interp.Nx = O["Nx"].as<double>(cfg.interp.Nx);
    cfg.interp.Ny = O["Ny"].as<double>(cfg.interp.Ny);
    cfg.interp.Nz = O["Nz"].as<double>(cfg.interp.Nz);
    cfg.interp.H1_project = O["H1_project"].as<bool>(cfg.interp.H1_project);
    cfg.interp.E = O["E"].as<bool>(cfg.interp.E);
    cfg.interp.V = O["V"].as<bool>(cfg.interp.V);
    cfg.interp.code1_mode = parse_interp_code1_mode(O["code1_mode"], cfg.interp.code1_mode);

    if (O["x_min"]) { cfg.interp.x_min = O["x_min"].as<double>(); }
    if (O["x_max"]) { cfg.interp.x_max = O["x_max"].as<double>(); }
    if (O["y_min"]) { cfg.interp.y_min = O["y_min"].as<double>(); }
    if (O["y_max"]) { cfg.interp.y_max = O["y_max"].as<double>(); }
    if (O["z_min"]) { cfg.interp.z_min = O["z_min"].as<double>(); }
    if (O["z_max"]) { cfg.interp.z_max = O["z_max"].as<double>(); }

    if ((cfg.interp.Nx == 0) || (cfg.interp.Ny == 0))
    {
        throw std::runtime_error("Interpolation input producess 0 elements in x or y");
    }

    auto validate_interp_axis = [](const char *axis, const double vmin, const double vmax)
    {
        const bool has_min = std::isfinite(vmin);
        const bool has_max = std::isfinite(vmax);

        if (has_min != has_max)
        {
            throw std::runtime_error(std::string("Interpolation input requires both interpolate.")
                                     + axis + "_min and interpolate." + axis + "_max");
        }
        if (has_min && !(vmax > vmin))
        {
            throw std::runtime_error(std::string("Interpolation input requires interpolate.")
                                     + axis + "_max > interpolate." + axis + "_min");
        }
    };

    validate_interp_axis("x", cfg.interp.x_min, cfg.interp.x_max);
    validate_interp_axis("y", cfg.interp.y_min, cfg.interp.y_max);
    validate_interp_axis("z", cfg.interp.z_min, cfg.interp.z_max);
}

// -----------------------------------------------------------------------------
// Debug settings
// -----------------------------------------------------------------------------
static void parse_debug_settings(Config &cfg, const YAML::Node &root)
{
    // Start from current config values
    DebugSettings dbg = cfg.debug;

    const auto dn = root["debug"];
    if (!dn) {
        cfg.debug = dbg;
        return;
    }

    // Primary flags (default to existing config values)
    dbg.debug   = dn["debug"]   ? dn["debug"].as<bool>(dbg.debug)     : dbg.debug;
    dbg.timing  = dn["timing"]  ? dn["timing"].as<bool>(dbg.timing)   : dbg.timing;
    dbg.dry_run = dn["dry_run"] ? dn["dry_run"].as<bool>(dbg.dry_run) : dbg.dry_run;

    const bool debug_enabled = dbg.debug;

    // Helper: read a bool, defaulting to current, then gate by debug flag
    auto gated = [&](const char *key, bool current) {
        bool value = current;
        const auto n = dn[key];
        if (n) {
            value = n.as<bool>(value);
        }
        return debug_enabled && value;
    };

    dbg.printBoundaryConditions = gated("printBoundaryConditions", dbg.printBoundaryConditions);
    dbg.printHypreWarnings      = gated("printHypreWarnings",      dbg.printHypreWarnings);
    dbg.dumpdata                = gated("dumpdata",                dbg.dumpdata);

    cfg.debug = dbg;
}

static void parse_voltage_settings(Config &cfg, const YAML::Node &root)
{
    VoltageSettings voltage = cfg.voltage;

    YAML::Node vn = root["voltage_config"];
    if (!vn) { vn = root["voltage"]; } // compatibility alias
    if (!vn) {
        cfg.voltage = voltage;
        return;
    }

    voltage.error_on_floating_after_resistor_chain =
        vn["error_on_floating_after_resistor_chain"]
            ? vn["error_on_floating_after_resistor_chain"].as<bool>(
                  voltage.error_on_floating_after_resistor_chain)
            : voltage.error_on_floating_after_resistor_chain;

    cfg.voltage = voltage;
}

static void parse_mesh_params(Config &cfg, const YAML::Node &root)
{
  if (!root["mesh"]) {
    throw std::runtime_error("Config error: missing mandatory field 'mesh'");
  }
  const YAML::Node mesh = root["mesh"];
  // mesh.path is optional to support geometry-driven/lazy mesh resolution.
  // Keep strict validation if the key is present.
  if (mesh["path"]) {
    cfg.mesh.path = mesh["path"].as<std::string>("");
    if (cfg.mesh.path.empty()) {
      Error("mesh.path must be non-empty when provided");
    }
  } else {
    cfg.mesh.path.clear();
  }
  if (mesh["AMR_TMOP_iter"])
  {
    cfg.mesh.AMR_TMOP_iter = mesh["AMR_TMOP_iter"].as<int>(cfg.mesh.AMR_TMOP_iter);
  }
  else if (mesh["AMR"] && mesh["AMR"]["enable"].as<bool>(cfg.mesh.amr.enable))
  {
    cfg.mesh.AMR_TMOP_iter = mesh["AMR"]["max_iter"].as<int>(cfg.mesh.amr.max_iter);
  }
  if (cfg.mesh.AMR_TMOP_iter < 1) Error("mesh.AMR_TMOP_iter must be at least 1");
  if (mesh["AMR"]) {
    cfg.mesh.amr.enable = mesh["AMR"]["enable"].as<bool>(cfg.mesh.amr.enable);
    cfg.mesh.amr.max_iter = mesh["AMR"]["max_iter"].as<int>(cfg.mesh.amr.max_iter);
    cfg.mesh.amr.local_error_goal = mesh["AMR"]["local_error_goal"].as<double>(cfg.mesh.amr.local_error_goal);
    cfg.mesh.amr.total_error_goal = mesh["AMR"]["total_error_goal"].as<double>(cfg.mesh.amr.total_error_goal);
    cfg.mesh.amr.total_error_fraction = mesh["AMR"]["total_error_fraction"].as<double>(cfg.mesh.amr.total_error_fraction);
    cfg.mesh.amr.total_error_norm_p = mesh["AMR"]["total_error_norm_p"].as<double>(cfg.mesh.amr.total_error_norm_p);
    cfg.mesh.amr.enable_derefine = mesh["AMR"]["enable_derefine"].as<bool>(cfg.mesh.amr.enable_derefine);
    cfg.mesh.amr.derefine_hysteresis = mesh["AMR"]["derefine_hysteresis"].as<double>(cfg.mesh.amr.derefine_hysteresis);
    cfg.mesh.amr.derefine_op = mesh["AMR"]["derefine_op"].as<int>(cfg.mesh.amr.derefine_op);
    cfg.mesh.amr.max_elements = mesh["AMR"]["max_elements"].as<long long>(cfg.mesh.amr.max_elements);
    cfg.mesh.amr.max_dofs = mesh["AMR"]["max_dofs"].as<long long>(cfg.mesh.amr.max_dofs);
    cfg.mesh.amr.nc_limit = mesh["AMR"]["nc_limit"].as<int>(cfg.mesh.amr.nc_limit);
    cfg.mesh.amr.prefer_conforming_refinement = mesh["AMR"]["prefer_conforming_refinement"].as<bool>(cfg.mesh.amr.prefer_conforming_refinement);
    cfg.mesh.amr.min_marked_elements = mesh["AMR"]["min_marked_elements"].as<int>(cfg.mesh.amr.min_marked_elements);
    cfg.mesh.amr.min_rel_error_reduction = mesh["AMR"]["min_rel_error_reduction"].as<double>(cfg.mesh.amr.min_rel_error_reduction);
    { std::string est = mesh["AMR"]["estimator"].as<std::string>("Kelly"); if (est == "Kelly") cfg.mesh.amr.estimator = AMRSettings::EstimatorType::Kelly; else if (est == "ZienkiewiczZhu") cfg.mesh.amr.estimator = AMRSettings::EstimatorType::ZienkiewiczZhu; else if (est == "L2ZienkiewiczZhu") cfg.mesh.amr.estimator = AMRSettings::EstimatorType::L2ZienkiewiczZhu; }
    cfg.mesh.amr.enable_rebalance = mesh["AMR"]["enable_rebalance"].as<bool>(cfg.mesh.amr.enable_rebalance);
    cfg.mesh.amr.verbose = mesh["AMR"]["verbose"].as<bool>(cfg.mesh.amr.verbose);
    cfg.mesh.amr.save_serial = mesh["AMR"]["save_serial"].as<bool>(cfg.mesh.amr.save_serial);
    cfg.mesh.amr.save_vtu_parallel = mesh["AMR"]["save_vtu_parallel"].as<bool>(cfg.mesh.amr.save_vtu_parallel);
  }
  if (mesh["TMOP"]) {
    cfg.mesh.tmop.enable = mesh["TMOP"]["enable"].as<bool>(cfg.mesh.tmop.enable);
    cfg.mesh.tmop.max_iter = mesh["TMOP"]["max_iter"].as<int>(cfg.mesh.tmop.max_iter);
    cfg.mesh.tmop.metric_id = mesh["TMOP"]["metric_id"].as<int>(cfg.mesh.tmop.metric_id);
    cfg.mesh.tmop.target_type = mesh["TMOP"]["target_type"].as<std::string>(cfg.mesh.tmop.target_type);
    cfg.mesh.tmop.optimizer = mesh["TMOP"]["optimizer"].as<std::string>(cfg.mesh.tmop.optimizer);
    cfg.mesh.tmop.step_limit = mesh["TMOP"]["step_limit"].as<double>(cfg.mesh.tmop.step_limit);
    cfg.mesh.tmop.rel_tol = mesh["TMOP"]["rel_tol"].as<double>(cfg.mesh.tmop.rel_tol);
    cfg.mesh.tmop.abs_tol = mesh["TMOP"]["abs_tol"].as<double>(cfg.mesh.tmop.abs_tol);
    cfg.mesh.tmop.verbose = mesh["TMOP"]["verbose"].as<bool>(cfg.mesh.tmop.verbose);

    if (cfg.mesh.tmop.max_iter < 1) Error("mesh.TMOP.max_iter must be at least 1");
    if (cfg.mesh.tmop.step_limit <= 0.0) Error("mesh.TMOP.step_limit must be > 0");
    if (cfg.mesh.tmop.rel_tol < 0.0) Error("mesh.TMOP.rel_tol must be >= 0");
    if (cfg.mesh.tmop.abs_tol < 0.0) Error("mesh.TMOP.abs_tol must be >= 0");
  }
}

static void parse_solver_params(Config &cfg, const YAML::Node &root)
{
    const auto s = root["solver"];
    if (!s) return;

    // Required/optional scalars with defaults
    cfg.solver.solver       = s["solver"].as<std::string>(cfg.solver.solver);
    cfg.solver.atol         = s["atol"].as<double>(cfg.solver.atol);
    cfg.solver.rtol         = s["rtol"].as<double>(cfg.solver.rtol);
    cfg.solver.maxiter      = s["maxiter"].as<int>(cfg.solver.maxiter);
    cfg.solver.printlevel   = s["printlevel"].as<int>(cfg.solver.printlevel);
    cfg.solver.axisymmetric = s["axisymmetric"].as<bool>(cfg.solver.axisymmetric);
    cfg.solver.generate_E   = s["generate_E"].as<bool>(cfg.solver.generate_E);

    // Optional overrides (keep existing stored defaults if absent)
    if (s["assembly_mode"])
        cfg.solver.assembly_mode = s["assembly_mode"].as<std::string>(cfg.solver.assembly_mode);

}

static void parse_material_params(Config &cfg, const YAML::Node &root)
{
    const auto mats = root["materials"];
    if (!mats) return;

    for (const auto &it : mats) {
        std::string name = it.first.as<std::string>();
        const auto node  = it.second;

        Material m;
        m.id        = node["attr_id"]   ? node["attr_id"].as<int>(-1)   : -1;
        if (!node["epsilon_r"]) {
            throw std::runtime_error(
                "Material '" + name + "' is missing epsilon_r (implicit defaults are disabled).");
        }
        m.epsilon_r = node["epsilon_r"].as<double>();
        if (!std::isfinite(m.epsilon_r) || m.epsilon_r <= 0.0) {
            throw std::runtime_error(
                "Material '" + name + "' has invalid epsilon_r=" +
                std::to_string(m.epsilon_r) +
                " (must be finite and > 0).");
        }

        cfg.materials[name] = m;
    }
}

static std::unordered_set<std::string>
collect_fieldcage_auto_assigned_boundaries(const YAML::Node &root)
{
    std::unordered_set<std::string> out;
    const YAML::Node fc = root["fieldcage_network"];
    if (!fc || !fc.IsMap()) {
        return out;
    }
    const bool enabled = fc["enabled"] ? fc["enabled"].as<bool>(true) : true;
    if (!enabled) {
        return out;
    }

    const YAML::Node nodes = fc["nodes"];
    if (!nodes || !nodes.IsSequence()) {
        return out;
    }

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const YAML::Node nd = nodes[i];
        if (!nd || !nd.IsMap()) continue;
        const bool fixed = nd["fixed"] ? nd["fixed"].as<bool>(false) : false;
        if (fixed) continue;
        const std::string bname = nd["boundary"] ? nd["boundary"].as<std::string>("") : "";
        if (!bname.empty()) {
            out.insert(bname);
        }
    }
    return out;
}

static void parse_boundary_params(Config &cfg, const YAML::Node &root)
{
    const auto bnds = root["boundaries"];
    if (!bnds) return;
    const auto fieldcage_auto = collect_fieldcage_auto_assigned_boundaries(root);

    for (const auto &it : bnds) {
        std::string name = it.first.as<std::string>();
        const auto node  = it.second;

        Boundary b;
        b.bdr_id = node["bdr_id"].as<int>(-1);
        b.type   = node["type"].as<std::string>("dirichlet");

        b.depth_dependent = node["depth_dependent"].as<bool>(b.depth_dependent);
        std::string type_l = b.type;
        std::transform(type_l.begin(), type_l.end(), type_l.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        const bool auto_fieldcage_dirichlet =
            (type_l == "dirichlet") && (fieldcage_auto.find(name) != fieldcage_auto.end());

        if (type_l == "dirichlet" || type_l == "neumann") {
            if (node["value"]) {
                b.value = node["value"].as<double>();
                if (!std::isfinite(b.value)) {
                    throw std::runtime_error(
                        "Boundary '" + name + "' has non-finite scalar value.");
                }
            } else if (!b.depth_dependent && !auto_fieldcage_dirichlet) {
                throw std::runtime_error(
                    "Boundary '" + name +
                    "' is missing explicit scalar value (implicit defaults are disabled).");
            }
        }

        if (b.depth_dependent) {
            if (!node["z_bot"] || !node["z_top"] || !node["value_bot"] || !node["value_top"]) {
                throw std::runtime_error(
                    "Boundary '" + name +
                    "' is depth-dependent and requires z_bot, z_top, value_bot, and value_top.");
            }
            b.z_bot     = node["z_bot"].as<double>();
            b.z_top     = node["z_top"].as<double>();
            b.value_bot = node["value_bot"].as<double>();
            b.value_top = node["value_top"].as<double>();

            if (!std::isfinite(b.z_bot) || !std::isfinite(b.z_top) ||
                !std::isfinite(b.value_bot) || !std::isfinite(b.value_top)) {
                throw std::runtime_error(
                    "Boundary '" + name + "' has non-finite depth-dependent parameters.");
            }
        } else {
            b.z_bot     = node["z_bot"] ? node["z_bot"].as<double>(b.z_bot) : b.z_bot;
            b.z_top     = node["z_top"] ? node["z_top"].as<double>(b.z_top) : b.z_top;
            b.value_bot = node["value_bot"] ? node["value_bot"].as<double>(b.value_bot) : b.value_bot;
            b.value_top = node["value_top"] ? node["value_top"].as<double>(b.value_top) : b.value_top;
        }

        if (b.bdr_id <= 0)
            throw std::runtime_error("Boundary '" + name + "' is missing a valid bdr_id");

        if (b.depth_dependent && (b.z_bot >= b.z_top))
            throw std::runtime_error("Boundary '" + name + "' depth dependent but z bounds are inconsistent");

        cfg.boundaries[name] = b;
    }
}
static void verify_cross_dependence(Config cfg)
{
    // Verify compute block 
    int world_size = 0;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    bool mpi_enabled = (world_size > 1);
    bool metrics_path = (cfg.run_mode == "metrics") || ((cfg.run_mode == "sim") && cfg.optimize.enabled);
    bool mpi_safe_method = (cfg.tracing_params.provider == "MPITracer") && ((cfg.civ_params.method != "RandomSample") && (cfg.civ_params.method != "Grid"));

    if (cfg.compute.threads.enabled && mpi_enabled)
        throw std::runtime_error("MPI and Threading via OpenMP simulataneously is not supported, use as many MPI nodes as you intend to use threads");

    if (mpi_enabled && mpi_safe_method)
        throw std::runtime_error("Fancy CIV methods not supported for MPI due to performance limitations of tracing (one to two machines will end up tracing at a time)");
}

// -----------------------------------------------------------------------------
// Internal common parser
// -----------------------------------------------------------------------------
static Config _LoadFromNode(const YAML::Node &root, Config cfg) {
  cfg.schema_version = root["schema_version"].as<int>(1);
  cfg.geometry_id    = root["geometry_id"].as<std::string>("");

  // --- Results save path 
  if (!root["save_path"]) {
    std::ostringstream oss;
    oss << "Config error: missing mandatory field 'save_path'";
    if (!root || !root.IsMap()) {
      oss << " (root is not a YAML map)";
    } else {
      oss << " (top-level keys:";
      bool any = false;
      for (auto it = root.begin(); it != root.end(); ++it) {
        std::string k;
        try { k = it->first.as<std::string>(); } catch (...) { k = "<non-scalar-key>"; }
        oss << (any ? ", " : " ") << k;
        any = true;
      }
      if (!any) oss << " <none>";
      oss << ")";
    }
    throw std::runtime_error(oss.str());
  }
  cfg.save_path = root["save_path"].as<std::string>();
  cfg.delete_files_present = root["delete_files_present"].as<bool>(false);

  parse_debug_settings(cfg, root);
  parse_voltage_settings(cfg, root);

  parse_mesh_params(cfg, root);

  parse_solver_params(cfg, root);
  parse_material_params(cfg, root);
  parse_boundary_params(cfg, root);
  
  parse_compute(cfg, root);
  parse_sweeps(cfg, root);
  parse_optimization(cfg, root);
  parse_fieldspread(cfg, root);
  parse_trace_params(cfg, root);
  parse_civ_params(cfg, root);
  parse_interp_params(cfg, root);

  parse_fieldcage_network(cfg, root);

  verify_cross_dependence(cfg);

  // TODO Need to parse all keys and check if not recognized ones are present

  return cfg;
}
static Config _LoadFromNode(const YAML::Node &root, std::string run_mode) {
    Config cfg;
    cfg.run_mode = run_mode;
    cfg = _LoadFromNode(root, cfg);
    return cfg;
}
static Config _LoadFromNode(const YAML::Node &root) {
    Config cfg;
    cfg = _LoadFromNode(root, cfg);
    return cfg;
}

// -----------------------------------------------------------------------------
// Public loaders
// -----------------------------------------------------------------------------
std::string ReadConfigString(const std::string& path, MPI_Comm comm)
{
    int rank = 0;
    MPI_Comm_rank(comm, &rank);

    int ok = 1;
    std::string payload;
    std::string err;

    if (rank == 0) {
        std::ifstream in(path, std::ios::in | std::ios::binary);
        if (!in) {
            ok = 0;
            err = "Config::Load: failed to open file: " + path;
        } else {
            std::ostringstream ss;
            ss << in.rdbuf();
            if (!in.good() && !in.eof()) {
                ok = 0;
                err = "Config::Load: failed while reading file: " + path;
            } else {
                payload = ss.str();
            }
        }
    }

    // Broadcast success/failure
    MPI_Bcast(&ok, 1, MPI_INT, 0, comm);

    // Broadcast error message if any
    if (!ok) {
        std::uint64_t elen = 0;
        if (rank == 0) elen = static_cast<std::uint64_t>(err.size());
        MPI_Bcast(&elen, 1, MPI_UINT64_T, 0, comm);

        err.resize(static_cast<std::size_t>(elen));
        if (elen > 0) {
            MPI_Bcast(err.data(), static_cast<int>(elen), MPI_CHAR, 0, comm);
        }

        // Keep ranks aligned before throwing
        MPI_Barrier(comm);
        throw std::runtime_error(err.empty()
                                     ? "Config::Load: unknown config read error"
                                     : err);
    }  

    // Broadcast payload size then payload bytes
    std::uint64_t n = 0;
    if (rank == 0) n = static_cast<std::uint64_t>(payload.size());
    MPI_Bcast(&n, 1, MPI_UINT64_T, 0, comm);

    std::string out;
    out.resize(static_cast<std::size_t>(n));
    if (rank == 0 && n > 0) {
        std::memcpy(out.data(), payload.data(), static_cast<std::size_t>(n));
    }
    if (n > 0) {
        MPI_Bcast(out.data(), static_cast<int>(n), MPI_CHAR, 0, comm);
    }

    if (out.empty()) {
        MPI_Barrier(comm);
        throw std::runtime_error("Config::Load: config file is empty: " + path);
    }

    MPI_Barrier(comm);
    return out;
}
Config Config::Load(const std::string& path)
{
    std::cout << "In load one args" << std::endl;
    const std::string yaml_str = ReadConfigString(path, MPI_COMM_WORLD);
    return LoadFromString(yaml_str);
}

Config Config::Load(const std::string& path, std::string run_mode)
{
    std::cout << "In load two args" << std::endl;
    const std::string yaml_str = ReadConfigString(path, MPI_COMM_WORLD);
    return LoadFromString(yaml_str, std::move(run_mode));
}
Config Config::LoadFromNode(const YAML::Node& root)
{
    Config cfg;
    return _LoadFromNode(root, cfg);
}
Config Config::LoadFromNode(const YAML::Node& root, std::string run_mode)
{
    YAML::Node root_copy = root;
    root_copy["run_mode"] = run_mode;
    Config cfg;
    // Not parsed by the loader as it's a command line option
    cfg.run_mode = std::move(run_mode);
    return _LoadFromNode(root_copy, cfg);
}
Config Config::LoadFromString(const std::string& yaml_str)
{
    YAML::Node root = YAML::Load(yaml_str);
    return LoadFromNode(root);
}

Config Config::LoadFromString(const std::string& yaml_str, std::string run_mode)
{
    YAML::Node root = YAML::Load(yaml_str);
    return LoadFromNode(root, std::move(run_mode));
}




// -----------------------------------------------------------------------------
// Solve Circuit network
// -----------------------------------------------------------------------------
void apply_fieldcage_network(Config &cfg)
{
    const auto &fc = cfg.fieldcage_network;
    if (!fc.enabled) {
        return;
    }

    auto fail = [](const std::string &msg) -> void {
        throw std::runtime_error("FieldCageNetwork: " + msg);
    };

    const auto &nodes = fc.nodes;
    const auto &edges = fc.edges;
    const auto &Rvals = fc.R_values;

    const int N = static_cast<int>(nodes.size());
    if (N == 0) {
        fail("enabled but no nodes are configured");
    }
    if (edges.empty()) {
        fail("enabled but no edges are configured");
    }

    // Map node name -> index
    std::unordered_map<std::string, int> node_index;
    node_index.reserve(N);
    for (int i = 0; i < N; ++i) {
        const std::string &name = nodes[i].name;
        if (name.empty()) {
            fail("node[" + std::to_string(i) + "] has an empty name");
        }
        auto [it, inserted] = node_index.emplace(name, i);
        if (!inserted) {
            fail("duplicate node name '" + name + "'");
        }
    }

    struct ValidEdge
    {
        int ni = -1;
        int nj = -1;
        double conductance = 0.0;
    };

    std::vector<ValidEdge> valid_edges;
    valid_edges.reserve(edges.size());
    std::vector<std::vector<int>> adjacency(N);

    for (const auto &e : edges) {
        auto it1 = node_index.find(e.n1);
        auto it2 = node_index.find(e.n2);
        if (it1 == node_index.end() || it2 == node_index.end()) {
            fail("edge (" + e.n1 + ", " + e.n2 + ") refers to an unknown node");
        }

        auto itR = Rvals.find(e.R_name);
        if (itR == Rvals.end()) {
            fail("edge (" + e.n1 + ", " + e.n2 + ") uses unknown resistor '" + e.R_name + "'");
        }

        const double R = itR->second;
        if (!(R > 0.0) || !std::isfinite(R)) {
            fail("resistor '" + e.R_name + "' must be finite and > 0");
        }

        const int ni = it1->second;
        const int nj = it2->second;
        valid_edges.push_back(ValidEdge{ni, nj, 1.0 / R});
        adjacency[ni].push_back(nj);
        adjacency[nj].push_back(ni);
    }

    // Classify fixed/free nodes and read fixed potentials
    std::vector<bool>  is_fixed(N, false);
    std::vector<double> V_fixed(N, 0.0);

    for (int i = 0; i < N; ++i) {
        const auto &nd = nodes[i];
        if (!nd.fixed) continue;

        if (nd.boundary.empty()) {
            fail("fixed node '" + nd.name + "' has no boundary assigned");
        }

        auto itb = cfg.boundaries.find(nd.boundary);
        if (itb == cfg.boundaries.end()) {
            fail("fixed node '" + nd.name + "' refers to unknown boundary '" + nd.boundary + "'");
        }

        is_fixed[i] = true;
        V_fixed[i]  = itb->second.value;
    }

    // Floating check: each connected component must have at least one fixed node.
    std::vector<int> visited(N, 0);
    std::vector<std::string> floating_nodes;

    for (int start = 0; start < N; ++start) {
        if (visited[start]) continue;

        std::vector<int> stack;
        std::vector<int> component;
        stack.push_back(start);
        visited[start] = 1;

        bool component_has_fixed = false;
        while (!stack.empty()) {
            const int v = stack.back();
            stack.pop_back();
            component.push_back(v);
            if (is_fixed[v]) component_has_fixed = true;

            for (int nb : adjacency[v]) {
                if (!visited[nb]) {
                    visited[nb] = 1;
                    stack.push_back(nb);
                }
            }
        }

        if (!component_has_fixed) {
            for (int idx : component) {
                floating_nodes.push_back(nodes[idx].name);
            }
        }
    }

    if (!floating_nodes.empty()) {
        std::sort(floating_nodes.begin(), floating_nodes.end());
        floating_nodes.erase(std::unique(floating_nodes.begin(), floating_nodes.end()),
                             floating_nodes.end());

        std::ostringstream oss;
        oss << "floating nodes after resistor-chain assignment (no fixed anchor in component): ";
        for (std::size_t i = 0; i < floating_nodes.size(); ++i) {
            if (i) oss << ", ";
            oss << floating_nodes[i];
        }

        if (cfg.voltage.error_on_floating_after_resistor_chain) {
            fail(oss.str());
        } else {
            Warning("FieldCageNetwork: " + oss.str());
            return;
        }
    }

    // Build list of free nodes and map node_index -> free_index
    std::vector<int> free_nodes;
    free_nodes.reserve(N);
    for (int i = 0; i < N; ++i) {
        if (!is_fixed[i]) free_nodes.push_back(i);
    }

    const int Nf = static_cast<int>(free_nodes.size());
    if (Nf == 0) {
        // All nodes are fixed; just mirror fixed values into boundaries.
        for (int i = 0; i < N; ++i) {
            const auto &nd = nodes[i];
            if (nd.boundary.empty()) continue;
            auto itb = cfg.boundaries.find(nd.boundary);
            if (itb == cfg.boundaries.end()) {
                fail("node '" + nd.name + "' refers to unknown boundary '" + nd.boundary + "'");
            }
            itb->second.value = V_fixed[i];
        }
        return;
    }

    // free index per node (-1 if fixed)
    std::vector<int> free_index_of_node(N, -1);
    for (int k = 0; k < Nf; ++k) {
        free_index_of_node[free_nodes[k]] = k;
    }

    // Build Ax=b for free-node voltages
    std::vector<double> A(Nf * Nf, 0.0);
    std::vector<double> b(Nf, 0.0);

    auto add_to_A = [&](int row, int col, double val) {
        A[row * Nf + col] += val;
    };

    for (const auto &edge : valid_edges) {
        const int ni = edge.ni;
        const int nj = edge.nj;
        const double G = edge.conductance;
        const int fi = free_index_of_node[ni];
        const int fj = free_index_of_node[nj];

        // both free
        if (fi >= 0 && fj >= 0) {
            add_to_A(fi, fi,  G);
            add_to_A(fj, fj,  G);
            add_to_A(fi, fj, -G);
            add_to_A(fj, fi, -G);
        }
        // i free, j fixed
        else if (fi >= 0 && fj < 0) {
            add_to_A(fi, fi, G);
            b[fi] += G * V_fixed[nj];
        }
        else if (fi < 0 && fj >= 0) {
            // i fixed, j free
            add_to_A(fj, fj, G);
            b[fj] += G * V_fixed[ni];
        }
        else {
            // both fixed
        }
    }

    // Gaussian elimination
    std::vector<double> x = b;
    const double pivot_tol = 1e-18;

    for (int k = 0; k < Nf; ++k) {
        int pivot = k;
        double max_abs = std::fabs(A[k * Nf + k]);
        for (int i = k + 1; i < Nf; ++i) {
            double val = std::fabs(A[i * Nf + k]);
            if (val > max_abs) {
                max_abs = val;
                pivot = i;
            }
        }

        if (max_abs <= pivot_tol) {
            fail("no unique solution (singular linear system) while solving resistor chain");
        }

        if (pivot != k) {
            for (int j = k; j < Nf; ++j) {
                std::swap(A[k * Nf + j], A[pivot * Nf + j]);
            }
            std::swap(x[k], x[pivot]);
        }

        const double Akk = A[k * Nf + k];

        for (int i = k + 1; i < Nf; ++i) {
            double factor = A[i * Nf + k] / Akk;
            if (factor == 0.0) continue;

            A[i * Nf + k] = 0.0;
            for (int j = k + 1; j < Nf; ++j) {
                A[i * Nf + j] -= factor * A[k * Nf + j];
            }
            x[i] -= factor * x[k];
        }
    }

    // Back substitute
    for (int i = Nf - 1; i >= 0; --i) {
        double sum = x[i];
        for (int j = i + 1; j < Nf; ++j) {
            sum -= A[i * Nf + j] * x[j];
        }
        const double Aii = A[i * Nf + i];
        if (std::fabs(Aii) <= pivot_tol) {
            fail("no unique solution (zero diagonal during back substitution)");
        }
        x[i] = sum / Aii;
    }

    // Assemble voltage vector
    std::vector<double> V(N, 0.0);
    for (int i = 0; i < N; ++i) {
        if (is_fixed[i]) {
            V[i] = V_fixed[i];
        }
    }
    for (int k = 0; k < Nf; ++k) {
        int ni = free_nodes[k];
        V[ni] = x[k];
    }

    // Write voltages back to boundary map
    for (int i = 0; i < N; ++i) {
        const auto &nd = nodes[i];
        if (nd.boundary.empty()) continue;
        auto itb = cfg.boundaries.find(nd.boundary);
        if (itb == cfg.boundaries.end()) {
            fail("node '" + nd.name + "' refers to unknown boundary '" + nd.boundary + "'");
        }
        itb->second.value = V[i];
    }
}

static const char* yaml_node_type_name(const YAML::Node& n) {
    if (!n) return "null";
    if (n.IsScalar()) return "scalar";
    if (n.IsSequence()) return "sequence";
    if (n.IsMap()) return "map";
    return "unknown";
}

void apply_fieldcage_network(YAML::Node &root)
{
    YAML::Node fc = root["fieldcage_network"];
    if (!fc || !fc.IsMap()) { return; }

    const bool enabled = fc["enabled"] ? fc["enabled"].as<bool>() : false;
    if (!enabled) { return; }

    YAML::Node nodes_node = fc["nodes"];
    YAML::Node edges_node = fc["edges"];

    // Support both names: "R_values" (old) and "resistors" (your YAML)
    YAML::Node rvals_node = fc["R_values"];
    if (!rvals_node) { rvals_node = fc["resistors"]; }

    if (!nodes_node || !nodes_node.IsSequence() || nodes_node.size() == 0) { return; }
    if (!edges_node || !edges_node.IsSequence() || edges_node.size() == 0) { return; }
    if (!rvals_node || !rvals_node.IsMap()) { return; }

    YAML::Node boundaries = root["boundaries"];
    if (!boundaries || !boundaries.IsMap()) { return; }

    struct NodeDesc {
        std::string name;
        bool fixed = false;
        std::string boundary;
    };
    struct EdgeDesc {
        std::string n1;
        std::string n2;
        std::string R_name; // resistor key
    };

    // Parse nodes
    const int N = static_cast<int>(nodes_node.size());
    std::vector<NodeDesc> nodes;
    nodes.reserve(N);

    for (std::size_t i = 0; i < nodes_node.size(); ++i) {
        YAML::Node nd = nodes_node[i];
        NodeDesc d;
        d.name     = nd["name"]     ? nd["name"].as<std::string>()     : std::string{};
        d.fixed    = nd["fixed"]    ? nd["fixed"].as<bool>()           : false;
        d.boundary = nd["boundary"] ? nd["boundary"].as<std::string>() : std::string{};

        if (d.name.empty()) { return; } // invalid config
        nodes.push_back(std::move(d));
    }

    // Parse edges (support both "R_name" and "R")
    std::vector<EdgeDesc> edges;
    edges.reserve(edges_node.size());

    for (std::size_t i = 0; i < edges_node.size(); ++i) {
        YAML::Node e = edges_node[i];
        EdgeDesc d;
        d.n1 = e["n1"] ? e["n1"].as<std::string>() : std::string{};
        d.n2 = e["n2"] ? e["n2"].as<std::string>() : std::string{};

        if (e["R_name"])      d.R_name = e["R_name"].as<std::string>();
        else if (e["R"])      d.R_name = e["R"].as<std::string>();
        else                  d.R_name.clear();

        if (d.n1.empty() || d.n2.empty() || d.R_name.empty()) { return; } // invalid config
        edges.push_back(std::move(d));
    }

    // Parse resistor values
    std::unordered_map<std::string, double> Rvals;
    Rvals.reserve(rvals_node.size());
    for (auto it = rvals_node.begin(); it != rvals_node.end(); ++it) {
        const std::string key = it->first.as<std::string>();
        const double val = it->second.as<double>();
        Rvals.emplace(key, val);
    }

    // Map node name -> index
    std::unordered_map<std::string, int> node_index;
    node_index.reserve(N);
    for (int i = 0; i < N; ++i) {
        const std::string &name = nodes[i].name;
        auto [it, inserted] = node_index.emplace(name, i);
        if (!inserted) {
            throw std::runtime_error("FieldCageNetwork: duplicate node name '" + name + "'");
        }
    }

    // Classify fixed vs free nodes and read fixed potentials from YAML boundaries
    std::vector<bool>   is_fixed(N, false);
    std::vector<double> V_fixed(N, 0.0);

    auto get_boundary_value = [&](const std::string &bname, double &out_val) -> bool {
        YAML::Node b = boundaries[bname];
        if (!b || !b.IsMap()) { return false; }
        if (!b["value"])      { return false; }
        out_val = b["value"].as<double>();
        return true;
    };

    for (int i = 0; i < N; ++i) {
        const auto &nd = nodes[i];
        if (!nd.fixed) { continue; }
        if (nd.boundary.empty()) { continue; }

        double vb = 0.0;
        if (!get_boundary_value(nd.boundary, vb)) {
            // Fixed node declared, but YAML has no boundary.value -> treat as not fixed.
            continue;
        }

        is_fixed[i] = true;
        V_fixed[i]  = vb;
    }

    // Build list of free nodes and map node_index -> free_index
    std::vector<int> free_nodes;
    free_nodes.reserve(N);
    for (int i = 0; i < N; ++i) {
        if (!is_fixed[i]) { free_nodes.push_back(i); }
    }

    const int Nf = static_cast<int>(free_nodes.size());
    if (Nf == 0) { return; }

    std::vector<int> free_index_of_node(N, -1);
    for (int k = 0; k < Nf; ++k) {
        free_index_of_node[free_nodes[k]] = k;
    }

    // Assemble Ax=b (row-major)
    std::vector<double> A(Nf * Nf, 0.0);
    std::vector<double> b(Nf, 0.0);

    auto add_to_A = [&](int row, int col, double val) {
        A[row * Nf + col] += val;
    };

    for (const auto &e : edges) {
        auto it1 = node_index.find(e.n1);
        auto it2 = node_index.find(e.n2);
        if (it1 == node_index.end() || it2 == node_index.end()) { continue; }

        auto itR = Rvals.find(e.R_name);
        if (itR == Rvals.end()) { continue; }

        const double R = itR->second;
        if (!(R > 0.0)) { continue; }

        const double G = 1.0 / R;

        const int ni = it1->second;
        const int nj = it2->second;

        const int fi = free_index_of_node[ni];
        const int fj = free_index_of_node[nj];

        if (fi >= 0 && fj >= 0) {
            add_to_A(fi, fi,  G);
            add_to_A(fj, fj,  G);
            add_to_A(fi, fj, -G);
            add_to_A(fj, fi, -G);
        }
        else if (fi >= 0 && fj < 0) {
            add_to_A(fi, fi, G);
            b[fi] += G * V_fixed[nj];
        }
        else if (fi < 0 && fj >= 0) {
            add_to_A(fj, fj, G);
            b[fj] += G * V_fixed[ni];
        }
        else {
            // both fixed -> no equation
        }
    }

    // Gaussian elimination: solve A x = b
    std::vector<double> x = b;

    for (int k = 0; k < Nf; ++k) {
        int pivot = k;
        double max_abs = std::fabs(A[k * Nf + k]);
        for (int i = k + 1; i < Nf; ++i) {
            double val = std::fabs(A[i * Nf + k]);
            if (val > max_abs) { max_abs = val; pivot = i; }
        }

        if (max_abs == 0.0) {
            // singular; bail out without writing anything
            return;
        }

        if (pivot != k) {
            for (int j = k; j < Nf; ++j) {
                std::swap(A[k * Nf + j], A[pivot * Nf + j]);
            }
            std::swap(x[k], x[pivot]);
        }

        const double Akk = A[k * Nf + k];
        for (int i = k + 1; i < Nf; ++i) {
            const double factor = A[i * Nf + k] / Akk;
            if (factor == 0.0) { continue; }
            A[i * Nf + k] = 0.0;
            for (int j = k + 1; j < Nf; ++j) {
                A[i * Nf + j] -= factor * A[k * Nf + j];
            }
            x[i] -= factor * x[k];
        }
    }

    for (int i = Nf - 1; i >= 0; --i) {
        double sum = x[i];
        for (int j = i + 1; j < Nf; ++j) {
            sum -= A[i * Nf + j] * x[j];
        }
        const double Aii = A[i * Nf + i];
        if (Aii == 0.0) { return; }
        x[i] = sum / Aii;
    }

    // Assemble voltage vector V for all nodes
    std::vector<double> V(N, 0.0);
    for (int i = 0; i < N; ++i) {
        if (is_fixed[i]) { V[i] = V_fixed[i]; }
    }
    for (int k = 0; k < Nf; ++k) {
        const int ni = free_nodes[k];
        V[ni] = x[k];
    }

    // Write back into YAML boundaries: boundaries[nd.boundary].value = V[i]
    for (int i = 0; i < N; ++i) {
        const auto &nd = nodes[i];
        if (nd.boundary.empty()) { continue; }

        YAML::Node bnd = boundaries[nd.boundary];
        if (!bnd || !bnd.IsMap()) { continue; }

        bnd["value"] = V[i]; // creates the key if missing
    }
}
