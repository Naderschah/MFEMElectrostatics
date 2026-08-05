#pragma once

// ===================================== Nelder Mead ==================================
/* Uses Git YibaiMeng/nelder-mead header only library 
Requires a function that takes const std::vector<T>& returning T 
No built in bounds or constraints -> We need to handle these manually
*/
#include <vector>
#include <string>
#include <utility>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <functional>
#include <stdexcept>
#include <limits>
#include <cmath> 
#include <chrono>
#include "mfem.hpp"

#include "solver_api.h"
#include "ConfigDocument.h"
#include "ComputeElectricField.h"
#include "config_modification.h"
#include "optimization.h"
#include "CIV.h"
#include "field_spread.h"
#include "nelder-mead.h" 
#include "path_handler.h" 



// Holds the optimization metrics
struct OptimizationMetrics {
    double CIV = 0.0;          // Charge Insensitive Volume
    double FieldSpread = 0.0; 
};
// For writing the meta file 
struct OptRunRecord
{
    std::string run_dir_name;    
    double objective_value = 0.;
    std::vector<std::pair<std::string, std::string>> vars;
};
// Internal result object
struct RunAndMetricsResult {
    std::string  run_dir_name;
    bool         success = false;
    SimulationResult sim; 
};

enum class FieldLineDest {
    Gate,       // reaches liquid-gas interface in TPC aperture
    PTFE,       // ends on PTFE
    Cathode,
    Other       // anything else (lost, ambiguous, etc.)
};

// Forward declare
class OptimizationLogger;

using ObjectiveFn = std::function<double(const OptimizationMetrics&)>;

void run_optimization(const Config &init_cfg, const ConfigDocument &base_doc);

RunAndMetricsResult run_simulation_and_save(const Config &cfg, std::size_t eval_index);

double evaluate_one_optimization_point(const ConfigDocument &base_doc, const OptimizationSettings &opt, const std::vector<double> &x, std::size_t eval_index, const ObjectiveFn &objective_fn, std::vector<OptRunRecord> &records, OptimizationLogger *logger);

ObjectiveFn make_objective_function(const OptimizationSettings &opt);

double compute_civ(const Config &cfg, const SimulationResult &result);

OptimizationMetrics compute_metrics(const Config &cfg, const SimulationResult &result);

void apply_opt_vars(YAML::Node &root, const OptimizationSettings &opt, const std::vector<double> &x);

std::vector<std::pair<std::string, std::string>> make_var_list(const OptimizationSettings &opt, const std::vector<double> &x);

// ---------------------- File writing

// Called at very end (kept in case file somehow gets corrupted)
void write_optimization_meta(const std::string &geometry_id, const std::filesystem::path &save_root, const std::vector<OptRunRecord> &records);

// Writer during optimization
void write_single_opt_record_block(std::ostream &meta, const std::filesystem::path &save_root, const OptRunRecord &rec);

class OptimizationLogger {
public:
    OptimizationLogger(std::string geometry_id,
                       std::filesystem::path save_root);

    // Create directories and write the header (<geometry_id>\n\n)
    void initialize();

    // Append a single OptRunRecord block to meta.txt
    void append_record(const OptRunRecord &rec);

    // Expose it in case requireds
    const std::filesystem::path &save_root() const {
        return save_root_;
    }

private:
    void ensure_initialized();

    std::string geometry_id_;
    std::filesystem::path save_root_;
    bool initialized_ = false;
};

void run_metrics_only(const Config &cfg);
