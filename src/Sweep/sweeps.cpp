#include "sweeps.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>

namespace
{
namespace fs = std::filesystem;

std::optional<fs::path> resolve_existing_file(const fs::path &candidate)
{
    if (candidate.empty()) return std::nullopt;
    std::error_code ec;
    if (!fs::exists(candidate, ec) || ec) return std::nullopt;
    if (!fs::is_regular_file(candidate, ec) || ec) return std::nullopt;
    return fs::absolute(candidate);
}

std::optional<fs::path> resolve_mesh_path_from_geometry_config(
    const YAML::Node &geom_root,
    const fs::path &geometry_config_path)
{
    const fs::path gc_dir = geometry_config_path.parent_path();

    const YAML::Node generation = geom_root["generation"];
    if (generation && generation.IsMap() && generation["output_mesh"]) {
        const std::string raw = generation["output_mesh"].as<std::string>("");
        if (!raw.empty()) {
            fs::path mesh_path(raw);
            if (mesh_path.is_relative()) mesh_path = gc_dir / mesh_path;
            if (const auto found = resolve_existing_file(mesh_path)) return found;
        }
    }

    std::string stem = geometry_config_path.stem().string();
    constexpr const char *suffix = "_geometry_config";
    if (stem.size() >= std::strlen(suffix) &&
        stem.compare(stem.size() - std::strlen(suffix), std::strlen(suffix), suffix) == 0) {
        stem.erase(stem.size() - std::strlen(suffix));
    }
    if (const auto found = resolve_existing_file(gc_dir / (stem + ".msh"))) {
        return found;
    }

    return std::nullopt;
}

std::string format_active_params(
    const std::vector<std::pair<std::string, std::string>> &active_params)
{
    if (active_params.empty()) return "(none)";
    std::ostringstream oss;
    for (std::size_t i = 0; i < active_params.size(); ++i) {
        if (i) oss << ", ";
        oss << active_params[i].first << "=" << active_params[i].second;
    }
    return oss.str();
}

bool sweep_skip_broken_enabled(const ConfigDocument &base_doc)
{
    const YAML::Node root = base_doc.Root();
    const YAML::Node mesh = root["mesh"];
    if (!mesh || !mesh.IsMap()) return true;

    // Legacy flat override
    if (mesh["geometry_sweep_skip_broken"]) {
        return mesh["geometry_sweep_skip_broken"].as<bool>(true);
    }

    const YAML::Node gs = mesh["geometry_sweep"];
    if (gs && gs.IsMap() && gs["skip_broken"]) {
        return gs["skip_broken"].as<bool>(true);
    }

    return true;
}

void merge_geometry_mapping(YAML::Node &root, const YAML::Node &geom_root)
{
    if (!geom_root || !geom_root.IsMap()) {
        throw std::runtime_error("Geometry config is not a YAML map.");
    }

    const YAML::Node geom_materials = geom_root["materials"];
    if (geom_materials && geom_materials.IsMap()) {
        if (!root["materials"] || !root["materials"].IsMap()) {
            root["materials"] = YAML::Node(YAML::NodeType::Map);
        }
        YAML::Node materials = root["materials"];

        for (auto it = geom_materials.begin(); it != geom_materials.end(); ++it) {
            const std::string name = it->first.as<std::string>("");
            const YAML::Node src = it->second;
            if (name.empty() || !src || !src.IsMap()) continue;

            if (!materials[name] || !materials[name].IsMap()) {
                materials[name] = YAML::Node(YAML::NodeType::Map);
            }
            YAML::Node dst = materials[name];
            if (src["attr_id"]) dst["attr_id"] = src["attr_id"].as<int>();
        }
    }

    const YAML::Node geom_boundaries = geom_root["boundaries"];
    if (geom_boundaries && geom_boundaries.IsMap()) {
        if (!root["boundaries"] || !root["boundaries"].IsMap()) {
            root["boundaries"] = YAML::Node(YAML::NodeType::Map);
        }
        YAML::Node boundaries = root["boundaries"];

        for (auto it = geom_boundaries.begin(); it != geom_boundaries.end(); ++it) {
            const std::string name = it->first.as<std::string>("");
            const YAML::Node src = it->second;
            if (name.empty() || !src || !src.IsMap()) continue;

            if (!boundaries[name] || !boundaries[name].IsMap()) {
                boundaries[name] = YAML::Node(YAML::NodeType::Map);
            }
            YAML::Node dst = boundaries[name];

            if (src["bdr_id"]) dst["bdr_id"] = src["bdr_id"].as<int>();
            if (!dst["type"]) {
                if (src["type"]) dst["type"] = src["type"].as<std::string>();
                else dst["type"] = "dirichlet";
            }
            if (!dst["value"] && src["value"]) {
                dst["value"] = src["value"].as<double>();
            }
        }
    }

    const YAML::Node geom_fc = geom_root["fieldcage_network"];
    if (geom_fc && geom_fc.IsMap()) {
        if (!root["fieldcage_network"] || !root["fieldcage_network"].IsMap()) {
            root["fieldcage_network"] = geom_fc;
        } else {
            YAML::Node fc = root["fieldcage_network"];
            for (auto it = geom_fc.begin(); it != geom_fc.end(); ++it) {
                const std::string key = it->first.as<std::string>("");
                if (key.empty()) continue;
                if (!fc[key]) fc[key] = it->second;
            }
        }
    }
}

YAML::Node retain_existing_map_entries_for_geometry_keys(
    const YAML::Node &existing_map,
    const YAML::Node &geometry_map)
{
    YAML::Node filtered(YAML::NodeType::Map);
    if (!geometry_map || !geometry_map.IsMap()) {
        return filtered;
    }
    if (!existing_map || !existing_map.IsMap()) {
        return filtered;
    }

    for (auto it = geometry_map.begin(); it != geometry_map.end(); ++it) {
        const std::string name = it->first.as<std::string>("");
        if (name.empty()) continue;
        const YAML::Node existing = existing_map[name];
        if (existing && existing.IsMap()) {
            filtered[name] = existing;
        }
    }
    return filtered;
}

void hydrate_leaf_geometry_mapping(YAML::Node &root)
{
    YAML::Node mesh = root["mesh"];
    if (!mesh || !mesh.IsMap() || !mesh["geometry_config"]) return;

    const std::string raw_gc = mesh["geometry_config"].as<std::string>("");
    if (raw_gc.empty()) return;

    const fs::path gc_path = fs::absolute(fs::path(raw_gc));
    auto found_gc = resolve_existing_file(gc_path);
    if (!found_gc) {
        throw std::runtime_error("sweep leaf geometry_config does not exist: " + gc_path.string());
    }

    YAML::Node geom_root = YAML::LoadFile(found_gc->string());

    // Sweep leaves start from the already-hydrated base config. When the next
    // geometry has fewer indexed boundaries (for example fewer field-shaping
    // rings), stale geometry-derived entries from the previous leaf can remain
    // and collide by bdr_id with the current geometry. Retain only entries
    // whose names still exist in the current leaf geometry config, then merge
    // fresh attr_id/bdr_id data for this geometry.
    root["materials"] = retain_existing_map_entries_for_geometry_keys(
        root["materials"], geom_root["materials"]);
    root["boundaries"] = retain_existing_map_entries_for_geometry_keys(
        root["boundaries"], geom_root["boundaries"]);

    merge_geometry_mapping(root, geom_root);

    // The field-cage network is geometry-dependent at sequence level. A
    // top-level "fill missing keys" merge is not sufficient when one sweep
    // point has more rings than another, because nodes/edges/R_values share
    // the same keys and would retain the previous leaf's shorter ladder.
    const YAML::Node geom_fc = geom_root["fieldcage_network"];
    if (geom_fc && geom_fc.IsMap()) {
        root["fieldcage_network"] = geom_fc;
    } else {
        root["fieldcage_network"] = YAML::Node();
    }

    const bool has_mesh_path = mesh["path"] && !mesh["path"].as<std::string>("").empty();
    if (!has_mesh_path) {
        if (const auto mp = resolve_mesh_path_from_geometry_config(geom_root, *found_gc)) {
            mesh["path"] = mp->string();
        } else {
            throw std::runtime_error(
                "Unable to resolve mesh.path for sweep leaf from geometry config: " +
                found_gc->string());
        }
    }
}

} // namespace

// -----------------------------------------------------------------------------
// Recursive sweep over Config
// -----------------------------------------------------------------------------
void sweep_recursive_cfg(const Config &base_cfg,
                         const ConfigDocument &base_doc,
                         const std::vector<SweepEntry> &sweeps,
                         std::size_t idx,
                         std::vector<std::pair<std::string, std::string>> &active_params, // (label, value)
                         std::vector<Assignment> &assignments,                           // (path, value)
                         bool skip_amr_in_run_one,
                         std::size_t &run_counter,
                         std::vector<RunRecord> &records)
{
    MPI_Comm comm = MPI_COMM_WORLD;
    int rank = 0;
    MPI_Comm_rank(comm, &rank);

    const int CMD_EVAL = 1;
    const int CMD_STOP = 0;
    const std::uint32_t SWEEP_MAGIC = 0x53E3E9A1u;
    const bool skip_broken = sweep_skip_broken_enabled(base_doc);

    // Parse string -> YAML scalar with best-effort typing (bool/int/double/string).
    auto parse_scalar = [](const std::string &s) -> YAML::Node {
        if (s == "true" || s == "True" || s == "TRUE")    return YAML::Node(true);
        if (s == "false" || s == "False" || s == "FALSE") return YAML::Node(false);

        { std::size_t pos = 0; try { long long v = std::stoll(s, &pos); if (pos == s.size()) return YAML::Node(v); } catch (...) {} }
        { std::size_t pos = 0; try { double v = std::stod(s, &pos);    if (pos == s.size()) return YAML::Node(v); } catch (...) {} }

        return YAML::Node(s);
    };

    // Set root[path] = value_node, where path is dot-separated.
    auto set_by_dot_path = [&](YAML::Node &root,
                               const std::string &path,
                               const YAML::Node &value_node)
    {
        std::stringstream ss(path);
        std::string key;
        std::vector<std::string> keys;
        while (std::getline(ss, key, '.')) if (!key.empty()) keys.push_back(key);

        if (keys.empty())
            throw std::runtime_error("sweep_recursive_cfg: empty assignment path");
        if (!root || !root.IsMap())
            throw std::runtime_error("sweep_recursive_cfg: root is not a map for path '" + path + "'");

        YAML::Node cur = root;
        for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
            const std::string &kk = keys[i];
            if (!cur[kk])
                throw std::runtime_error("sweep_recursive_cfg: invalid path '" + path + "' (missing key '" + kk + "')");
            if (!cur[kk].IsMap())
                throw std::runtime_error("sweep_recursive_cfg: path '" + path + "' traverses non-map key '" + kk + "'");
            cur.reset(cur[kk]);
        }
        cur[keys.back()] = value_node;
    };

    // ---------------------------------------------------------------------
    // Workers: enter the service loop exactly once (top-level call only).
    // Root broadcasts: MAGIC, CMD, then payload for each leaf.
    // ---------------------------------------------------------------------
    if (rank != 0)
    {
        if (idx != 0) return;

        while (true)
        {
            std::uint32_t magic = 0;
            MPI_Bcast(&magic, 1, MPI_UINT32_T, 0, comm);
            if (magic != SWEEP_MAGIC) {
                MPI_Abort(comm, 1);
            }

            int cmd = CMD_STOP;
            MPI_Bcast(&cmd, 1, MPI_INT, 0, comm);
            if (cmd == CMD_STOP) break;

            std::uint64_t run_counter_u64 = 0;
            MPI_Bcast(&run_counter_u64, 1, MPI_UINT64_T, 0, comm);

            std::uint64_t yaml_len_u64 = 0;
            MPI_Bcast(&yaml_len_u64, 1, MPI_UINT64_T, 0, comm);

            if (yaml_len_u64 > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                MPI_Abort(comm, 2);
            }

            std::string run_config_str;
            run_config_str.resize(static_cast<std::size_t>(yaml_len_u64));
            if (yaml_len_u64 > 0) {
                MPI_Bcast(run_config_str.data(), static_cast<int>(yaml_len_u64), MPI_CHAR, 0, comm);
            }

            std::uint64_t nparams_u64 = 0;
            MPI_Bcast(&nparams_u64, 1, MPI_UINT64_T, 0, comm);
            const std::size_t nparams = static_cast<std::size_t>(nparams_u64);

            auto bcast_string = [&](std::string &s) {
                std::uint64_t len_u64 = 0;
                MPI_Bcast(&len_u64, 1, MPI_UINT64_T, 0, comm);

                if (len_u64 > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                    MPI_Abort(comm, 3);
                }

                s.resize(static_cast<std::size_t>(len_u64));
                if (len_u64 > 0) {
                    MPI_Bcast(s.data(), static_cast<int>(len_u64), MPI_CHAR, 0, comm);
                }
            };

            std::vector<std::pair<std::string, std::string>> active_params_recv;
            active_params_recv.reserve(nparams);

            for (std::size_t i = 0; i < nparams; ++i) {
                std::string k, v;
                bcast_string(k);
                bcast_string(v);
                active_params_recv.emplace_back(std::move(k), std::move(v));
            }

            // make_run_folder() uses MPI_COMM_WORLD internally, so all ranks must call it here.
            YAML::Node yaml_root = YAML::Load(run_config_str);
            fs::path run_path = make_run_folder(yaml_root, static_cast<int>(run_counter_u64));
            yaml_root["save_path"] = run_path.string();

            bool local_ok = true;
            try {
                (void)run_one(base_cfg,
                              yaml_root,
                              active_params_recv,
                              static_cast<std::size_t>(run_counter_u64),
                              skip_amr_in_run_one);
            } catch (...) {
                local_ok = false;
            }

            int local_ok_i = local_ok ? 1 : 0;
            int global_ok_i = 0;
            MPI_Allreduce(&local_ok_i, &global_ok_i, 1, MPI_INT, MPI_MIN, comm);

            // Keep protocol lockstep between leaf evaluations.
            MPI_Barrier(comm);
        }

        return;
    }

    // ---------------------------------------------------------------------
    // Root: recurse to enumerate sweep points; at each leaf broadcast work.
    // ---------------------------------------------------------------------
    if (idx == sweeps.size())
    {
        auto skip_leaf = [&](const std::string &reason) {
            if (rank == 0) {
                std::cerr << "[sweep skip] run_index=" << run_counter
                          << " params={" << format_active_params(active_params) << "}"
                          << " reason: " << reason << "\n";
            }
            ++run_counter;
        };

        std::string run_config_str;
        try {
            YAML::Node root = base_doc.Root();
            for (const auto &a : assignments) {
                set_by_dot_path(root, a.path, parse_scalar(a.value));
            }
            // Per-leaf merge ensures geometry/material/boundary IDs follow
            // the geometry_config assigned by this sweep point.
            hydrate_leaf_geometry_mapping(root);

            // Do not call make_run_folder() before broadcasting: it contains WORLD collectives.
            // All ranks call make_run_folder() after receiving the YAML payload.
            YAML::Emitter out;
            out << root;
            run_config_str = out.c_str();
        } catch (const std::exception &e) {
            if (!skip_broken) throw;
            skip_leaf(e.what());
            return;
        } catch (...) {
            if (!skip_broken) throw;
            skip_leaf("unknown pre-run preparation failure");
            return;
        }

        // Header: MAGIC then CMD (workers validate stream / allow STOP).
        {
            std::uint32_t magic = SWEEP_MAGIC;
            MPI_Bcast(&magic, 1, MPI_UINT32_T, 0, comm);

            int cmd = CMD_EVAL;
            MPI_Bcast(&cmd, 1, MPI_INT, 0, comm);
        }

        std::uint64_t run_counter_u64 = static_cast<std::uint64_t>(run_counter);
        MPI_Bcast(&run_counter_u64, 1, MPI_UINT64_T, 0, comm);

        std::uint64_t yaml_len_u64 = static_cast<std::uint64_t>(run_config_str.size());
        MPI_Bcast(&yaml_len_u64, 1, MPI_UINT64_T, 0, comm);

        if (yaml_len_u64 > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
            MPI_Abort(comm, 2);
        }

        if (yaml_len_u64 > 0) {
            MPI_Bcast(run_config_str.data(), static_cast<int>(yaml_len_u64), MPI_CHAR, 0, comm);
        }

        auto bcast_string_root = [&](const std::string &s) {
            std::uint64_t len_u64 = static_cast<std::uint64_t>(s.size());
            MPI_Bcast(&len_u64, 1, MPI_UINT64_T, 0, comm);

            if (len_u64 > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
                MPI_Abort(comm, 3);
            }

            if (len_u64 > 0) {
                MPI_Bcast(const_cast<char*>(s.data()), static_cast<int>(len_u64), MPI_CHAR, 0, comm);
            }
        };

        std::uint64_t nparams_u64 = static_cast<std::uint64_t>(active_params.size());
        MPI_Bcast(&nparams_u64, 1, MPI_UINT64_T, 0, comm);

        for (const auto &kv : active_params) {
            bcast_string_root(kv.first);
            bcast_string_root(kv.second);
        }

        // All ranks must call make_run_folder() (WORLD collectives inside).
        YAML::Node yaml_root = YAML::Load(run_config_str);
        fs::path run_path = make_run_folder(yaml_root, static_cast<int>(run_counter_u64));
        yaml_root["save_path"] = run_path.string();

        std::string run_dir_name;
        bool local_ok = true;
        std::string local_error;
        try {
            run_dir_name = run_one(base_cfg, yaml_root, active_params, run_counter, skip_amr_in_run_one);
        } catch (const std::exception &e) {
            local_ok = false;
            local_error = e.what();
        } catch (...) {
            local_ok = false;
            local_error = "unknown run_one failure";
        }

        int local_ok_i = local_ok ? 1 : 0;
        int global_ok_i = 0;
        MPI_Allreduce(&local_ok_i, &global_ok_i, 1, MPI_INT, MPI_MIN, comm);
        const bool global_ok = (global_ok_i == 1);

        MPI_Barrier(comm);

        if (global_ok)
        {
            if (!run_dir_name.empty())
            {
                RunRecord rec;
                rec.run_dir_name = run_path;
                rec.params = active_params;
                records.push_back(std::move(rec));
            }
        }
        else
        {
            if (!skip_broken) {
                if (!local_error.empty()) throw std::runtime_error(local_error);
                throw std::runtime_error("sweep point failed on at least one MPI rank");
            }
            if (rank == 0) {
                const std::string reason =
                    local_error.empty() ? "failure reported on one or more MPI ranks" : local_error;
                std::cerr << "[sweep skip] run_index=" << run_counter
                          << " params={" << format_active_params(active_params) << "}"
                          << " reason: " << reason << "\n";
            }
        }

        ++run_counter;
        return;
    }

    const SweepEntry &sw = sweeps[idx];
    const std::string label = sw.name.empty() ? sw.path : sw.name;

    switch (sw.kind)
    {
        case SweepEntry::Kind::Discrete:
        {
            for (const auto &val : sw.values)
            {
                active_params.emplace_back(label, val);
                assignments.push_back({ sw.path, val });

                sweep_recursive_cfg(base_cfg, base_doc, sweeps, idx + 1,
                                    active_params, assignments, skip_amr_in_run_one, run_counter, records);

                assignments.pop_back();
                active_params.pop_back();
            }
            break;
        }

        case SweepEntry::Kind::Range:
        {
            if (sw.steps <= 1)
            {
                std::string val_str = std::to_string(sw.start);

                active_params.emplace_back(label, val_str);
                assignments.push_back({ sw.path, val_str });

                sweep_recursive_cfg(base_cfg, base_doc, sweeps, idx + 1,
                                    active_params, assignments, skip_amr_in_run_one, run_counter, records);

                assignments.pop_back();
                active_params.pop_back();
            }
            else
            {
                double step = (sw.end - sw.start) / double(sw.steps - 1);
                for (int i = 0; i < sw.steps; ++i)
                {
                    double v = sw.start + i * step;
                    std::string val_str = std::to_string(v);

                    active_params.emplace_back(label, val_str);
                    assignments.push_back({ sw.path, val_str });

                    sweep_recursive_cfg(base_cfg, base_doc, sweeps, idx + 1,
                                        active_params, assignments, skip_amr_in_run_one, run_counter, records);

                    assignments.pop_back();
                    active_params.pop_back();
                }
            }
            break;
        }

        case SweepEntry::Kind::Fixed:
        {
            for (const auto &cfg : sw.configs)
            {
                active_params.emplace_back(label, cfg.label);

                const std::size_t n_added = cfg.assigns.size();
                for (const auto &a : cfg.assigns) assignments.push_back(a);

                sweep_recursive_cfg(base_cfg, base_doc, sweeps, idx + 1,
                                    active_params, assignments, skip_amr_in_run_one, run_counter, records);

                for (std::size_t k = 0; k < n_added; ++k) assignments.pop_back();
                active_params.pop_back();
            }
            break;
        }
    }

    // Top-level only: send STOP. Do not add a barrier here; workers are waiting in Bcast(magic).
    if (idx == 0)
    {
        std::uint32_t magic = SWEEP_MAGIC;
        MPI_Bcast(&magic, 1, MPI_UINT32_T, 0, comm);

        int cmd = CMD_STOP;
        MPI_Bcast(&cmd, 1, MPI_INT, 0, comm);
    }
}




// -----------------------------------------------------------------------------
// Global meta.txt in save_root
// -----------------------------------------------------------------------------
void write_sweep_meta(const std::string &geometry_id,
                      const std::filesystem::path &save_root,
                      const std::vector<RunRecord> &records)
{
    MPI_Comm comm = MPI_COMM_WORLD;
    int rank = 0;
    MPI_Comm_rank(comm, &rank);

    if (rank == 0)
    {
        std::error_code ec;
        std::filesystem::create_directories(save_root, ec);
        if (ec)
        {
            std::cerr << "Warning: could not create save_root directory "
                      << save_root << " : " << ec.message() << "\n";
        }

        std::ofstream meta(save_root / "meta.txt");
        if (!meta)
        {
            std::cerr << "Warning: could not open meta.txt in " << save_root << "\n";
        }
        else
        {
            meta << geometry_id << "\n\n";

            if (records.empty())
            {
                meta << "(no runs)\n";
            }
            else
            {
                for (const auto &rec : records)
                {
                    meta << rec.run_dir_name << ":\n";
                    if (rec.params.empty())
                    {
                        meta << "  (none)\n\n";
                    }
                    else
                    {
                        for (const auto &p : rec.params)
                        {
                            meta << "  " << p.first << " = " << p.second << "\n";
                        }
                        meta << "\n";
                    }
                }
            }

            meta.flush();
        }
    }

    MPI_Barrier(comm);
}
