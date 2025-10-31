#pragma once
#include <string>
#include <unordered_map>

struct Boundary {
    int id = -1;            // bdr_id
    std::string type;       // "dirichlet" | "neumann" | "robin"
    double value = 0.0;
};

struct Material {
    int id = -1;            // attr_id
    double epsilon_r = 1.0;
};

struct DeviceSettings {
    bool use_gpu = false;
    int threads = 20;
};

struct DebugSettings {
    bool debug = false;
    bool quick_mesh = false;
};

struct MeshSettings {
    std::string path = "geometry.msh";
};

struct SolverSettings {
    // MFEM / solve controls
    int    order = 3;                 // NOTE: now under solver
    double atol = 1.0;
    double rtol = 0.0;
    int    maxiter = 100000;
    int    printlevel = 1;

    // Outputs
    std::string mesh_save_path   = "simulation_mesh.msh";
    std::string V_solution_path  = "solution_V.gf";
    std::string Emag_solution_path = "solution_Emag.gf";
};

struct Config {
    std::string mesh_path = "geometry.msh";
    DeviceSettings device;
    DebugSettings  debug;
    SolverSettings solver;

    std::unordered_map<std::string, Boundary> boundaries;
    std::unordered_map<std::string, Material> materials;

    static Config Load(const std::string& path);
};

