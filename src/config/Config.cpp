// src/config/Config.cpp
#include "Config.h"
#include <yaml-cpp/yaml.h>
#include <stdexcept>
#include <iostream>

static int read_order_with_backcompat(const YAML::Node& root, int dflt)
{
    // Preferred location
    if (root["solver"] && root["solver"]["order"])
        return root["solver"]["order"].as<int>(dflt);

    return dflt;
}

Config Config::Load(const std::string& path) {
    YAML::Node root = YAML::LoadFile(path);
    Config cfg;

    // --- Mesh path
    if (root["mesh"] && root["mesh"]["path"])
        cfg.mesh.path = root["mesh"]["path"].as<std::string>("geometry.msh");

    // --- Device
    if (root["device"]) {
        cfg.device.use_gpu = root["device"]["use_gpu"].as<bool>(false);
        cfg.device.threads = root["device"]["threads"].as<int>(20);
    }

    // --- Debug
    if (root["debug"]) {
        cfg.debug.debug      = root["debug"]["debug"].as<bool>(false);
        cfg.debug.quick_mesh = root["debug"]["quick_mesh"].as<bool>(false);
    }

    // --- Solver (including outputs)
    if (root["solver"]) {
        const auto s = root["solver"];
        cfg.solver.atol        = s["atol"].as<double>(1.0);
        cfg.solver.rtol        = s["rtol"].as<double>(0.0);
        cfg.solver.maxiter     = s["maxiter"].as<int>(100000);
        cfg.solver.printlevel  = s["printlevel"].as<int>(1);

        cfg.solver.mesh_save_path    = s["mesh_save_path"].as<std::string>("simulation_mesh.msh");
        cfg.solver.V_solution_path   = s["V_solution_path"].as<std::string>("solution_V.gf");
        cfg.solver.Emag_solution_path= s["Emag_solution_path"].as<std::string>("solution_Emag.gf");
    }
    // order supports both new (solver.order) and legacy (fem.order)
    cfg.solver.order = read_order_with_backcompat(root, /*dflt*/3);

    // --- Materials
    if (root["materials"]) {
        for (auto it : root["materials"]) {
            std::string name = it.first.as<std::string>();
            auto node = it.second;
            Material m;
            m.id = node["attr_id"] ? node["attr_id"].as<int>(-1) : -1; // "Default" may omit attr_id
            m.epsilon_r = node["epsilon_r"].as<double>(1.0);
            cfg.materials[name] = m;
        }
    }

    // --- Boundaries
    if (root["boundaries"]) {
        for (auto it : root["boundaries"]) {
            std::string name = it.first.as<std::string>();
            auto node = it.second;
            Boundary b;
            b.id    = node["bdr_id"].as<int>(-1);
            b.type  = node["type"].as<std::string>("dirichlet");
            b.value = node["value"].as<double>(0.0);
            if (b.id <= 0) {
                throw std::runtime_error("Boundary '" + name + "' is missing a valid bdr_id");
            }
            cfg.boundaries[name] = b;
        }
    }

    return cfg;
}

