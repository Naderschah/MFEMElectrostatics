#pragma once 

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "Config.h"
#include "ConfigDocument.h"
#include "config_modification.h"
#include "solver_api.h"
#include "path_handler.h" 

// Recursive function executing each sweep combination
void sweep_recursive_cfg(
    const Config& base_cfg,
    const ConfigDocument& base_doc,
    const std::vector<SweepEntry>& sweeps,
    std::size_t idx,
    std::vector<std::pair<std::string, std::string>>& active_params,
    std::vector<Assignment>& assignments,
    bool skip_amr_in_run_one,
    std::size_t& run_counter,
    std::vector<RunRecord>& records
);

// Writes the global sweep metadata file (meta.txt) into save_root.
void write_sweep_meta(
    const std::string& geometry_id,
    const std::filesystem::path& save_root,
    const std::vector<RunRecord>& records
);
