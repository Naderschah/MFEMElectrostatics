#pragma once

#include <memory>
#include <filesystem>
#include <string>
#include <vector>
#include <utility>
#include <cstddef>
#include <stdexcept>

#include "Config.h"
#include "mfem.hpp"

struct SimulationResult
{
    bool success = false;
    std::string error_message;
    bool adaptive_converged = true;
    bool adaptive_iteration_limit_reached = false;
    int adaptive_iterations = 0;

    std::unique_ptr<mfem::ParMesh>                 mesh;
    std::unique_ptr<mfem::FiniteElementCollection> fec;
    std::unique_ptr<mfem::ParFiniteElementSpace>   pfes;

    std::unique_ptr<mfem::ParGridFunction> V;
    std::unique_ptr<mfem::ParGridFunction> E;
    std::unique_ptr<mfem::ParGridFunction> Emag;
};

// Struct to hold boundary attributes
struct BoundaryData
{
  // Marker arrays: length = max_bdr_attr
  mfem::Array<int> dirichlet_marker;
  mfem::Array<int> neumann_marker;
  mfem::Array<int> surface_charge_marker;

  // Base coefficients (unweighted). 
  std::unique_ptr<mfem::Coefficient> Vd_coeff;     // Dirichlet value u_D
  std::unique_ptr<mfem::Coefficient> gN_coeff;     // Neumann flux data (if used)
  std::unique_ptr<mfem::Coefficient> sigma_coeff;  // Surface charge σ_s

  // Piecewise coefficients
  std::vector<std::unique_ptr<mfem::Coefficient>> owned_pieces_Vd;
  std::vector<std::unique_ptr<mfem::Coefficient>> owned_pieces_gN;
  std::vector<std::unique_ptr<mfem::Coefficient>> owned_pieces_sigma;

  // Dispatcher flags
  bool has_dirichlet = false;
  bool has_neumann = false;
  bool has_surface_charge = false;
};

std::string PrecomputeAMRMesh(const Config &cfg, int precision = 16);

// Core solver entry point (no CLI, no file I/O)
SimulationResult run_simulation(
    std::shared_ptr<Config> cfg,
    const std::filesystem::path& model_path,
    bool skip_amr);

void save_results(const SimulationResult &result, const std::filesystem::path &root_path, YAML::Node yaml_config);

SimulationResult load_results(const Config &cfg, const std::filesystem::path &root_path);

std::string run_one(const Config& cfg, YAML::Node yaml_root, const std::vector<std::pair<std::string, std::string>>& active_params, std::size_t run_index, bool skip_amr = false);
