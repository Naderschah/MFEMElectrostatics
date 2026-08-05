#include <iostream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include <omp.h>

#include "cmdLineInteraction.h"
#include "Config.h"
#include "ConfigDocument.h"
#include "optimization.h"
#include "sweeps.h"
#include "parallelization.h"
#include "geometry_api.h"
#if HAVE_VTK
    #include "plotting_api.h"
#endif
#include "interpolator.h"
#include "path_handler.h"

namespace
{

struct GeometryConfigResolution
{
    std::filesystem::path path;
    bool explicit_path = false;
    std::string source;
};

struct VoltageApplyResult
{
    std::set<std::string> matched_boundaries;
};

static std::filesystem::path resolve_against_dir(const std::filesystem::path &maybe_relative,
                                                 const std::filesystem::path &base_dir)
{
    if (maybe_relative.empty()) {
        return maybe_relative;
    }
    if (maybe_relative.is_absolute()) {
        return maybe_relative;
    }
    return base_dir / maybe_relative;
}

static std::string to_lower_copy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static std::string trim_copy(const std::string &s)
{
    std::size_t b = 0;
    while (b < s.size() && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    std::size_t e = s.size();
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

static std::string normalize_optional_path_scalar(std::string raw)
{
    raw = trim_copy(raw);
    if (raw.empty()) return raw;
    const std::string lowered = to_lower_copy(raw);
    if (lowered == "null" || lowered == "~") return "";
    return raw;
}

static bool ends_with(const std::string &s, const std::string &suffix)
{
    if (suffix.size() > s.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

static std::optional<std::filesystem::path>
find_geometry_config_in_root(const std::filesystem::path &root_dir,
                             const std::string &geometry_name)
{
    namespace fs = std::filesystem;
    if (!fs::exists(root_dir) || !fs::is_directory(root_dir)) {
        return std::nullopt;
    }
    if (geometry_name.empty()) {
        return std::nullopt;
    }

    struct ScoredPath
    {
        int score = -1;
        fs::path path;
    };
    ScoredPath best;

    const std::string geometry_l = to_lower_copy(geometry_name);
    const std::string exact_a = geometry_l + "_geometry_config.yaml";
    const std::string exact_b = geometry_l + "_gmsh_geometry_config.yaml";

    auto maybe_update = [&](const fs::path &p, int score) {
        std::error_code ec;
        if (!fs::exists(p, ec) || ec) return;
        if (!fs::is_regular_file(p, ec) || ec) return;
        if (score > best.score) {
            best.score = score;
            best.path = fs::absolute(p);
        }
    };

    const std::vector<fs::path> preferred_dirs = {
        root_dir,
        root_dir / "mesh",
        root_dir / "geometry" / "mesh",
        root_dir / "geometry",
    };
    for (const auto &dir : preferred_dirs) {
        maybe_update(dir / (geometry_name + "_geometry_config.yaml"), 100);
        maybe_update(dir / (geometry_name + "_gmsh_geometry_config.yaml"), 95);
    }
    // If a strong direct candidate was found, avoid expensive recursive scan.
    if (best.score >= 95) {
        return best.path;
    }

    std::error_code it_ec;
    for (fs::recursive_directory_iterator it(
             root_dir,
             fs::directory_options::skip_permission_denied,
             it_ec),
         end;
         it != end; it.increment(it_ec)) {
        if (it_ec) {
            it_ec.clear();
            continue;
        }
        std::error_code ec;
        if (!it->is_regular_file(ec) || ec) continue;

        const std::string fname_l = to_lower_copy(it->path().filename().string());
        if (!ends_with(fname_l, "_geometry_config.yaml")) continue;

        int score = -1;
        if (fname_l == exact_a) {
            score = 110;
        } else if (fname_l == exact_b) {
            score = 105;
        } else if (fname_l.rfind(geometry_l, 0) == 0) {
            score = 80;
        } else if (fname_l.find(geometry_l) != std::string::npos) {
            score = 60;
        } else {
            continue;
        }

        const std::string path_l = to_lower_copy(it->path().string());
        if (path_l.find("/mesh/") != std::string::npos) score += 5;
        if (path_l.find("gmsh") != std::string::npos) score += 2;

        maybe_update(it->path(), score);
    }

    if (best.score < 0) return std::nullopt;
    return best.path;
}

static void append_unique_path(std::vector<std::filesystem::path> &out,
                               const std::filesystem::path &candidate)
{
    if (candidate.empty()) return;
    const auto abs = std::filesystem::absolute(candidate);
    if (std::find(out.begin(), out.end(), abs) == out.end()) {
        out.push_back(abs);
    }
}

static std::vector<std::filesystem::path>
build_geometry_search_bases(const YAML::Node &root,
                            const std::optional<std::filesystem::path> &exe_dir_opt,
                            const std::filesystem::path &config_dir)
{
    std::vector<std::filesystem::path> out;

    const YAML::Node mesh = root["mesh"];
    if (mesh && mesh.IsMap()) {
        if (mesh["geometry_config_search_root"]) {
            const YAML::Node n = mesh["geometry_config_search_root"];
            if (!n.IsScalar()) {
                throw std::runtime_error(
                    "mesh.geometry_config_search_root must be a string path.");
            }
            const std::string raw = n.as<std::string>("");
            if (!raw.empty()) {
                append_unique_path(out, resolve_against_dir(std::filesystem::path(raw), config_dir));
            }
        }
        if (mesh["geometry_config_search_roots"]) {
            const YAML::Node roots = mesh["geometry_config_search_roots"];
            if (!roots.IsSequence()) {
                throw std::runtime_error(
                    "mesh.geometry_config_search_roots must be a sequence of string paths.");
            }
            for (std::size_t i = 0; i < roots.size(); ++i) {
                if (!roots[i].IsScalar()) {
                    throw std::runtime_error(
                        "mesh.geometry_config_search_roots entries must be string paths.");
                }
                const std::string raw = roots[i].as<std::string>("");
                if (raw.empty()) continue;
                append_unique_path(out, resolve_against_dir(std::filesystem::path(raw), config_dir));
            }
        }
    }

    append_unique_path(out, config_dir);
    append_unique_path(out, config_dir.parent_path());
    append_unique_path(out, config_dir.parent_path().parent_path());

    if (exe_dir_opt && !exe_dir_opt->empty()) {
        append_unique_path(out, *exe_dir_opt);
        append_unique_path(out, exe_dir_opt->parent_path());
        append_unique_path(out, exe_dir_opt->parent_path().parent_path());
    }
    return out;
}

static std::optional<std::filesystem::path>
resolve_geometry_config_from_geometry_name(const YAML::Node &root,
                                           const std::optional<std::filesystem::path> &exe_dir_opt,
                                           const std::filesystem::path &config_dir)
{
    const YAML::Node mesh = root["mesh"];
    std::string geometry_name;
    if (mesh && mesh.IsMap() && mesh["geometry"]) {
        geometry_name = mesh["geometry"].as<std::string>("");
    }
    if (geometry_name.empty() && root["geometry_id"]) {
        geometry_name = root["geometry_id"].as<std::string>("");
    }
    if (geometry_name.empty()) {
        return std::nullopt;
    }

    const auto bases = build_geometry_search_bases(root, exe_dir_opt, config_dir);
    if (bases.empty()) return std::nullopt;

    const std::vector<std::filesystem::path> root_suffixes = {
        std::filesystem::path{},
        "geometry",
        "XEMFEM_geometry",
        "XEMFEM_geometries",
        std::filesystem::path("geometry") / "XEMFEM_geometry",
        std::filesystem::path("geometry") / "XEMFEM_geometries",
    };

    for (const auto &base : bases) {
        for (const auto &suffix : root_suffixes) {
            const auto root_dir = base / suffix;
            const auto found = find_geometry_config_in_root(root_dir, geometry_name);
            if (found) return found;
        }
    }

    return std::nullopt;
}

static std::string extract_geometry_name(const YAML::Node &root)
{
    const YAML::Node mesh = root["mesh"];
    if (mesh && mesh.IsMap() && mesh["geometry"]) {
        const std::filesystem::path p(mesh["geometry"].as<std::string>(""));
        const std::string ext = to_lower_copy(p.extension().string());
        if (ext == ".json" || ext == ".dxf") {
            return p.stem().string();
        }
        return p.string();
    }
    if (root["geometry_id"]) {
        return root["geometry_id"].as<std::string>("");
    }
    return "";
}

static std::vector<std::string> geometry_source_subdirs_from_root(const YAML::Node &root)
{
    std::vector<std::string> source_subdirs = {"geometries", "json_slices_pruned", "json_slices"};
    const YAML::Node mesh = root["mesh"];
    YAML::Node subdirs;
    if (mesh && mesh.IsMap()) {
        if (mesh["geometry_source_subdirs"]) {
            subdirs = mesh["geometry_source_subdirs"];
        } else if (mesh["geometry_json_subdirs"]) {
            subdirs = mesh["geometry_json_subdirs"];
        }
    }
    if (subdirs) {
        if (!subdirs.IsSequence()) {
            throw std::runtime_error("mesh.geometry_source_subdirs must be a sequence of strings.");
        }
        source_subdirs.clear();
        for (std::size_t i = 0; i < subdirs.size(); ++i) {
            if (!subdirs[i].IsScalar()) {
                throw std::runtime_error("mesh.geometry_source_subdirs entries must be strings.");
            }
            const std::string sub = subdirs[i].as<std::string>("");
            if (sub.empty()) continue;
            source_subdirs.push_back(sub);
        }
        if (source_subdirs.empty()) {
            throw std::runtime_error(
                "mesh.geometry_source_subdirs is empty; provide at least one subdirectory name.");
        }
    }
    return source_subdirs;
}

static std::optional<std::filesystem::path>
resolve_geometry_source_from_geometry_name(
    const YAML::Node &root,
    const std::optional<std::filesystem::path> &exe_dir_opt,
    const std::filesystem::path &config_dir,
    std::vector<std::filesystem::path> *searched_dirs = nullptr)
{
    const std::string geometry_name = extract_geometry_name(root);
    if (geometry_name.empty()) {
        return std::nullopt;
    }
    std::vector<std::string> candidate_names;
    const YAML::Node mesh = root["mesh"];
    if (mesh && mesh.IsMap() && mesh["geometry"]) {
        const std::filesystem::path raw(mesh["geometry"].as<std::string>(""));
        const std::string raw_ext = to_lower_copy(raw.extension().string());
        if (raw_ext == ".json" || raw_ext == ".dxf") {
            candidate_names.push_back(raw.filename().string());
        }
    }
    if (candidate_names.empty()) {
        candidate_names.push_back(geometry_name + ".dxf");
        candidate_names.push_back(geometry_name + ".json");
    }

    const auto bases = build_geometry_search_bases(root, exe_dir_opt, config_dir);
    if (bases.empty()) return std::nullopt;

    const std::vector<std::filesystem::path> source_roots = {
        std::filesystem::path{},
        "XEMFEM_geometries",
        std::filesystem::path("geometry") / "XEMFEM_geometries",
        "XEMFEM_geometry",
        std::filesystem::path("geometry") / "XEMFEM_geometry",
    };
    const std::vector<std::string> source_subdirs = geometry_source_subdirs_from_root(root);

    for (const auto &base : bases) {
        for (const auto &prefix : source_roots) {
            for (const auto &subdir : source_subdirs) {
                const std::filesystem::path dir = base / prefix / subdir;
                if (searched_dirs) searched_dirs->push_back(std::filesystem::absolute(dir));
                for (const auto &candidate_name : candidate_names) {
                    const std::filesystem::path candidate = dir / candidate_name;
                    std::error_code ec;
                    if (std::filesystem::exists(candidate, ec) &&
                        !ec &&
                        std::filesystem::is_regular_file(candidate, ec) &&
                        !ec) {
                        return std::filesystem::absolute(candidate);
                    }
                }
            }
        }
    }

    return std::nullopt;
}

static void bcast_string_world(std::string &s, const int root_rank = 0)
{
    std::uint64_t n = static_cast<std::uint64_t>(s.size());
    MPI_Bcast(&n, 1, MPI_UINT64_T, root_rank, MPI_COMM_WORLD);
    if (n > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("broadcast string too large");
    }
    s.resize(static_cast<std::size_t>(n));
    if (n > 0) {
        MPI_Bcast(s.data(), static_cast<int>(n), MPI_CHAR, root_rank, MPI_COMM_WORLD);
    }
}

static geometry::MeshingOptions
build_meshing_options_from_yaml(const YAML::Node &root)
{
    geometry::MeshingOptions options;
    options.writeGeometryConfig = true;

    const YAML::Node debug = root["debug"];
    if (debug && debug.IsMap()) {
        options.debug = debug["debug"].as<bool>(options.debug);
    }

    const YAML::Node mesh = root["mesh"];
    if (mesh && mesh.IsMap() && mesh["gmsh"] && mesh["gmsh"].IsMap()) {
        const YAML::Node gmsh = mesh["gmsh"];
        options.dimension = gmsh["dim"].as<int>(options.dimension);
        options.suppressGmshTerminalOutput =
            gmsh["suppress_terminal_output"].as<bool>(options.suppressGmshTerminalOutput);
        options.debug = gmsh["debug"].as<bool>(options.debug);
        options.writeGeometryConfig =
            gmsh["write_geometry_config"].as<bool>(options.writeGeometryConfig);

        const YAML::Node option_numbers = gmsh["option_numbers"];
        if (option_numbers && option_numbers.IsMap()) {
            for (auto it = option_numbers.begin(); it != option_numbers.end(); ++it) {
                const std::string key = it->first.as<std::string>("");
                if (key.empty()) continue;
                options.optionNumbers[key] = it->second.as<double>();
            }
        }

        if (gmsh["threads"]) {
            if (!gmsh["threads"].IsScalar()) {
                throw std::runtime_error("mesh.gmsh.threads must be an integer >= 1.");
            }
            const int threads = gmsh["threads"].as<int>(0);
            if (threads < 1) {
                throw std::runtime_error("mesh.gmsh.threads must be an integer >= 1.");
            }
            options.optionNumbers["General.NumThreads"] = static_cast<double>(threads);
        }

        const YAML::Node option_strings = gmsh["option_strings"];
        if (option_strings && option_strings.IsMap()) {
            for (auto it = option_strings.begin(); it != option_strings.end(); ++it) {
                const std::string key = it->first.as<std::string>("");
                if (key.empty()) continue;
                options.optionStrings[key] = it->second.as<std::string>("");
            }
        }
    }

    if (mesh && mesh.IsMap()) {
        if (mesh["fieldcage_network_from_geometry"]) {
            options.fieldCageNetworkFromGeometry =
                mesh["fieldcage_network_from_geometry"].as<bool>(
                    options.fieldCageNetworkFromGeometry);
        } else if (mesh["merge_geometry_fieldcage_network"]) {
            options.fieldCageNetworkFromGeometry =
                mesh["merge_geometry_fieldcage_network"].as<bool>(
                    options.fieldCageNetworkFromGeometry);
        } else if (mesh["gmsh"] && mesh["gmsh"].IsMap() &&
                   mesh["gmsh"]["autogenerate_fieldcage_network"]) {
            options.fieldCageNetworkFromGeometry =
                mesh["gmsh"]["autogenerate_fieldcage_network"].as<bool>(
                    options.fieldCageNetworkFromGeometry);
        }
    }

    return options;
}

static void maybe_generate_geometry_from_source(
    ConfigDocument &config_doc,
    const std::filesystem::path &config_dir,
    const std::optional<std::filesystem::path> &exe_dir_opt)
{
    YAML::Node &root = config_doc.Root();
    const YAML::Node mesh = root["mesh"];
    if (!mesh || !mesh.IsMap()) return;

    const std::string geometry_name = extract_geometry_name(root);
    if (geometry_name.empty()) return;

    const std::string mesh_path_raw =
        mesh["path"] ? normalize_optional_path_scalar(mesh["path"].as<std::string>("")) : "";
    const std::string geometry_config_raw =
        mesh["geometry_config"] ? normalize_optional_path_scalar(mesh["geometry_config"].as<std::string>("")) : "";
    const bool has_mesh_path = !mesh_path_raw.empty();
    const bool has_geometry_config = !geometry_config_raw.empty();

    // Keep old explicit-precompiled behavior when geometry_config is explicitly pinned.
    if (has_geometry_config) {
        return;
    }

    if (has_mesh_path) {
        std::cout << "[geometry generate] overriding existing mesh.path with source-generated mesh for geometry='"
                  << geometry_name << "'\n";
    }

    std::vector<std::filesystem::path> searched_source_dirs;
    const auto source_path_opt = resolve_geometry_source_from_geometry_name(
        root, exe_dir_opt, config_dir, &searched_source_dirs);
    if (!source_path_opt) {
        const std::vector<std::string> source_subdirs = geometry_source_subdirs_from_root(root);
        std::ostringstream oss;
        std::ostringstream dirs_oss;
        for (std::size_t i = 0; i < source_subdirs.size(); ++i) {
            if (i) dirs_oss << ", ";
            dirs_oss << source_subdirs[i];
        }
        oss << "Could not resolve geometry source for geometry '" << geometry_name
            << "'. Expected file '" << geometry_name
            << ".dxf' or '" << geometry_name
            << ".json' under one of these subdirectories: "
            << dirs_oss.str()
            << ".";
        if (!searched_source_dirs.empty()) {
            oss << " Searched geometry source dirs in order: ";
            for (std::size_t i = 0; i < searched_source_dirs.size(); ++i) {
                if (i) oss << ", ";
                oss << searched_source_dirs[i].string();
            }
            oss << ".";
        }
        throw std::runtime_error(oss.str());
    }

    const std::filesystem::path source_path = std::filesystem::absolute(*source_path_opt);
    const std::string source_mode = to_lower_copy(source_path.extension().string());
    const std::string geometry_id = source_path.stem().string();
    const std::string save_path_raw = root["save_path"].as<std::string>("");
    if (save_path_raw.empty()) {
        throw std::runtime_error(
            "Cannot generate mesh from geometry source: save_path is missing or empty.");
    }
    const std::filesystem::path save_root =
        std::filesystem::absolute(resolve_against_dir(std::filesystem::path(save_path_raw), config_dir));
    const std::filesystem::path output_dir = save_root / "mesh";
    const std::filesystem::path output_mesh = output_dir / (geometry_id + "_gmsh.msh");

    double liquid_level = std::numeric_limits<double>::quiet_NaN();
    if (mesh["liquid_level"]) {
        liquid_level = mesh["liquid_level"].as<double>(liquid_level);
    }

    const geometry::MeshingOptions options = build_meshing_options_from_yaml(root);

    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    std::string generated_mesh_path;
    std::string generated_geometry_config_path;
    std::string generation_error;

    if (rank == 0) {
        try {
            std::filesystem::create_directories(output_dir);
            std::cout << "[geometry generate] mode=" << source_mode.substr(1)
                      << " geometry='" << geometry_name << "'\n";
            std::cout << "[geometry generate] source='" << source_path.string() << "'\n";
            std::cout << "[geometry generate] output dir='" << output_dir.string() << "'\n";
            const auto thread_it = options.optionNumbers.find("General.NumThreads");
            if (thread_it != options.optionNumbers.end()) {
                std::cout << "[geometry generate] gmsh threads=" << thread_it->second
                          << " (rank0)\n";
            }
            const auto generated =
                geometry::make_mesh(source_path, output_mesh, liquid_level, options);
            generated_mesh_path = std::filesystem::absolute(generated.meshPath).string();
            if (!generated.geometryConfigPath) {
                throw std::runtime_error(
                    "geometry::make_mesh did not produce a geometry config path.");
            }
            generated_geometry_config_path =
                std::filesystem::absolute(*generated.geometryConfigPath).string();
            std::cout << "[geometry generate] generated mesh='"
                      << generated_mesh_path << "'\n";
            std::cout << "[geometry generate] generated geometry_config='"
                      << generated_geometry_config_path << "'\n";
        } catch (const std::exception &e) {
            generation_error = e.what();
        }
    }

    bcast_string_world(generation_error, 0);
    if (!generation_error.empty()) {
        throw std::runtime_error(
            "Failed to generate mesh/config from geometry source: " + generation_error);
    }
    bcast_string_world(generated_mesh_path, 0);
    bcast_string_world(generated_geometry_config_path, 0);

    config_doc.SetPath("mesh.path", YAML::Node(generated_mesh_path), true);
    config_doc.SetPath("mesh.geometry_config", YAML::Node(generated_geometry_config_path), true);
}

static bool wildcard_match_simple(const std::string &pattern, const std::string &text)
{
    std::size_t p = 0;
    std::size_t t = 0;
    std::size_t star = std::string::npos;
    std::size_t match = 0;

    while (t < text.size()) {
        if (p < pattern.size() &&
            (pattern[p] == text[t] || pattern[p] == '?')) {
            ++p;
            ++t;
            continue;
        }
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            match = t;
            continue;
        }
        if (star != std::string::npos) {
            p = star + 1;
            t = ++match;
            continue;
        }
        return false;
    }

    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

static bool is_auxiliary_slice_stem(const std::string &stem)
{
    // Auxiliary artifacts produced by cleanup/salvage pipelines are not
    // standalone simulation geometries and must not be included in sweeps.
    const std::string s = to_lower_copy(stem);
    return s.find("_preprune_") != std::string::npos ||
           s.find("_salvage_") != std::string::npos;
}

static std::vector<std::string> collect_geometry_sweep_names(
    const YAML::Node &root,
    const std::optional<std::filesystem::path> &exe_dir_opt,
    const std::filesystem::path &config_dir)
{
    const YAML::Node mesh = root["mesh"];
    if (!mesh || !mesh.IsMap()) return {};

    const YAML::Node gs = mesh["geometry_sweep"];
    const bool legacy_enabled =
        mesh["geometry_sweep_all"] && mesh["geometry_sweep_all"].as<bool>(false);
    const bool enabled =
        legacy_enabled || (gs && gs.IsMap() && gs["enabled"] && gs["enabled"].as<bool>(false));
    if (!enabled) return {};

    std::set<std::string> names;
    if (gs && gs.IsMap() && gs["names"]) {
        const YAML::Node n = gs["names"];
        if (!n.IsSequence()) {
            throw std::runtime_error("mesh.geometry_sweep.names must be a sequence of geometry names.");
        }
        for (std::size_t i = 0; i < n.size(); ++i) {
            const std::string v = n[i].as<std::string>("");
            if (!v.empty()) names.insert(v);
        }
    }

    bool include_all = true;
    if (gs && gs.IsMap() && gs["all"]) {
        include_all = gs["all"].as<bool>(include_all);
    } else if (!names.empty()) {
        include_all = false;
    }
    if (!include_all) {
        return std::vector<std::string>(names.begin(), names.end());
    }

    std::vector<std::filesystem::path> scan_dirs;
    auto append_unique_dir = [&](const std::filesystem::path &candidate) {
        if (candidate.empty()) return;
        const auto abs = std::filesystem::absolute(candidate);
        if (std::find(scan_dirs.begin(), scan_dirs.end(), abs) == scan_dirs.end()) {
            scan_dirs.push_back(abs);
        }
    };

    if (gs && gs.IsMap() && gs["json_dir"]) {
        if (!gs["json_dir"].IsScalar()) {
            throw std::runtime_error("mesh.geometry_sweep.json_dir must be a string path.");
        }
        const std::string raw = gs["json_dir"].as<std::string>("");
        if (!raw.empty()) {
            append_unique_dir(resolve_against_dir(std::filesystem::path(raw), config_dir));
        }
    } else {
        const auto bases = build_geometry_search_bases(root, exe_dir_opt, config_dir);
        const std::vector<std::filesystem::path> json_roots = {
            std::filesystem::path{},
            "XEMFEM_geometries",
            std::filesystem::path("geometry") / "XEMFEM_geometries",
            "XEMFEM_geometry",
            std::filesystem::path("geometry") / "XEMFEM_geometry",
        };
        const std::vector<std::string> json_subdirs = geometry_source_subdirs_from_root(root);
        for (const auto &base : bases) {
            for (const auto &prefix : json_roots) {
                for (const auto &subdir : json_subdirs) {
                    append_unique_dir(base / prefix / subdir);
                }
            }
        }
    }

    std::string glob = "slice_*";
    if (gs && gs.IsMap() && gs["glob"]) {
        if (!gs["glob"].IsScalar()) {
            throw std::runtime_error("mesh.geometry_sweep.glob must be a string.");
        }
        const std::string raw_glob = gs["glob"].as<std::string>("");
        if (!raw_glob.empty()) glob = raw_glob;
    }

    for (const auto &dir : scan_dirs) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || ec) continue;
        if (!std::filesystem::is_directory(dir, ec) || ec) continue;

        for (std::filesystem::directory_iterator it(
                 dir, std::filesystem::directory_options::skip_permission_denied, ec),
             end;
             it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }
            std::error_code item_ec;
            if (!it->is_regular_file(item_ec) || item_ec) continue;
            const auto file = it->path().filename().string();
            if (!wildcard_match_simple(glob, file)) continue;
            const std::string ext = to_lower_copy(it->path().extension().string());
            if (ext != ".json" && ext != ".dxf") continue;
            const std::string stem = it->path().stem().string();
            if (ext == ".json" && is_auxiliary_slice_stem(stem)) continue;
            if (!stem.empty()) names.insert(stem);
        }
    }

    return std::vector<std::string>(names.begin(), names.end());
}

static void maybe_expand_geometry_sweep(ConfigDocument &config_doc,
                                        const std::filesystem::path &config_dir,
                                        const std::optional<std::filesystem::path> &exe_dir_opt)
{
    YAML::Node &root = config_doc.Root();
    const YAML::Node mesh = root["mesh"];
    if (!mesh || !mesh.IsMap()) return;

    const YAML::Node gs = mesh["geometry_sweep"];
    const bool legacy_enabled =
        mesh["geometry_sweep_all"] && mesh["geometry_sweep_all"].as<bool>(false);
    const bool enabled =
        legacy_enabled || (gs && gs.IsMap() && gs["enabled"] && gs["enabled"].as<bool>(false));
    if (!enabled) return;
    bool skip_broken = true;
    if (mesh["geometry_sweep_skip_broken"]) {
        skip_broken = mesh["geometry_sweep_skip_broken"].as<bool>(true);
    }
    if (gs && gs.IsMap() && gs["skip_broken"]) {
        skip_broken = gs["skip_broken"].as<bool>(skip_broken);
    }

    if (gs && gs.IsMap() && gs["expanded"] && gs["expanded"].as<bool>(false)) {
        return;
    }

    const std::vector<std::string> geometry_names =
        collect_geometry_sweep_names(root, exe_dir_opt, config_dir);
    if (geometry_names.empty()) {
        throw std::runtime_error(
            "mesh.geometry_sweep is enabled but no geometries were discovered.");
    }

    YAML::Node sweep_entry(YAML::NodeType::Map);
    sweep_entry["name"] = "geometry";
    sweep_entry["kind"] = "fixed";
    YAML::Node configs(YAML::NodeType::Sequence);

    const std::string current_geometry = extract_geometry_name(root);
    const YAML::Node current_mesh = root["mesh"];
    const std::string current_mesh_path =
        current_mesh && current_mesh["path"]
            ? normalize_optional_path_scalar(current_mesh["path"].as<std::string>(""))
            : "";
    const std::string current_geometry_config_path =
        current_mesh && current_mesh["geometry_config"]
            ? normalize_optional_path_scalar(current_mesh["geometry_config"].as<std::string>(""))
            : "";
    std::vector<std::string> skipped_geometries;

    for (const auto &geometry_name : geometry_names) {
        try {
            std::string mesh_path;
            std::string geometry_config_path;

            if (geometry_name == current_geometry &&
                !current_mesh_path.empty() &&
                !current_geometry_config_path.empty()) {
                mesh_path = current_mesh_path;
                geometry_config_path = current_geometry_config_path;
            } else {
                ConfigDocument per_geom = ConfigDocument::FromNode(root);
                per_geom.SetPath("mesh.geometry", YAML::Node(geometry_name), true);
                per_geom.SetPath("mesh.path", YAML::Node(), true);
                per_geom.SetPath("mesh.geometry_config", YAML::Node(), true);
                maybe_generate_geometry_from_source(per_geom, config_dir, exe_dir_opt);

                const YAML::Node per_root = per_geom.Root();
                const YAML::Node per_mesh = per_root["mesh"];
                mesh_path = per_mesh && per_mesh["path"]
                              ? normalize_optional_path_scalar(per_mesh["path"].as<std::string>(""))
                              : "";
                geometry_config_path = per_mesh && per_mesh["geometry_config"]
                                         ? normalize_optional_path_scalar(per_mesh["geometry_config"].as<std::string>(""))
                                         : "";
            }

            if (mesh_path.empty() || geometry_config_path.empty()) {
                throw std::runtime_error(
                    "geometry sweep could not resolve artifacts for geometry '" +
                    geometry_name + "'.");
            }

            YAML::Node cfg(YAML::NodeType::Map);
            cfg["label"] = geometry_name;
            YAML::Node set(YAML::NodeType::Map);
            set["mesh.geometry"] = geometry_name;
            set["mesh.path"] = mesh_path;
            set["mesh.geometry_config"] = geometry_config_path;
            cfg["set"] = set;
            configs.push_back(cfg);
        } catch (const std::exception &e) {
            if (!skip_broken) throw;
            skipped_geometries.push_back(geometry_name);
            std::cout << "[geometry sweep skip] geometry='" << geometry_name
                      << "' reason: " << e.what() << "\n";
        }
    }

    if (configs.size() == 0) {
        std::ostringstream oss;
        oss << "mesh.geometry_sweep: no valid geometries remain after expansion";
        if (!skipped_geometries.empty()) {
            oss << " (skipped " << skipped_geometries.size() << ")";
        }
        throw std::runtime_error(oss.str());
    }

    sweep_entry["configs"] = configs;

    // Ensure hydration/voltage resolution can use a valid geometry mapping
    // before sweep execution starts.
    const YAML::Node first_cfg = configs[0];
    const YAML::Node first_set = first_cfg["set"];
    const std::string first_label =
        first_cfg && first_cfg["label"] ? first_cfg["label"].as<std::string>("") : "";
    const std::string first_mesh =
        first_set && first_set["mesh.path"] ? normalize_optional_path_scalar(first_set["mesh.path"].as<std::string>("")) : "";
    const std::string first_gc =
        first_set && first_set["mesh.geometry_config"]
            ? normalize_optional_path_scalar(first_set["mesh.geometry_config"].as<std::string>(""))
            : "";
    if (!first_label.empty()) config_doc.SetPath("mesh.geometry", YAML::Node(first_label), true);
    if (!first_mesh.empty()) config_doc.SetPath("mesh.path", YAML::Node(first_mesh), true);
    if (!first_gc.empty()) config_doc.SetPath("mesh.geometry_config", YAML::Node(first_gc), true);

    YAML::Node merged_sweeps(YAML::NodeType::Sequence);
    merged_sweeps.push_back(sweep_entry);
    const YAML::Node existing_sweeps = root["sweeps"];
    if (existing_sweeps && existing_sweeps.IsSequence()) {
        for (std::size_t i = 0; i < existing_sweeps.size(); ++i) {
            merged_sweeps.push_back(existing_sweeps[i]);
        }
    }
    root["sweeps"] = merged_sweeps;

    config_doc.SetPath("mesh.geometry_sweep.expanded", YAML::Node(true), true);
    std::cout << "[geometry sweep] discovered=" << geometry_names.size()
              << " included=" << configs.size()
              << " skipped=" << skipped_geometries.size()
              << " skip_broken=" << (skip_broken ? "true" : "false")
              << "\n";
}

static std::optional<GeometryConfigResolution>
resolve_geometry_config_path(const YAML::Node &root,
                             const std::filesystem::path &config_dir,
                             const std::optional<std::filesystem::path> &exe_dir_opt)
{
    const YAML::Node mesh = root["mesh"];
    if (!mesh || !mesh.IsMap()) {
        return std::nullopt;
    }

    // 1) Explicit path from config
    if (mesh["geometry_config"]) {
        const std::string raw =
            normalize_optional_path_scalar(mesh["geometry_config"].as<std::string>(""));
        if (!raw.empty()) {
            std::cout << "[geometry resolve] explicit mesh.geometry_config='" << raw << "'\n";
        }
        if (!raw.empty()) {
            GeometryConfigResolution out;
            out.path = resolve_against_dir(std::filesystem::path(raw), config_dir);
            out.explicit_path = true;
            out.source = "mesh.geometry_config";
            return out;
        }
    }

    // 2) Derived from mesh.path: <mesh_stem>_geometry_config.yaml
    if (mesh["path"]) {
        const std::string raw_mesh_path =
            normalize_optional_path_scalar(mesh["path"].as<std::string>(""));
        if (!raw_mesh_path.empty()) {
            std::filesystem::path mesh_path =
                resolve_against_dir(std::filesystem::path(raw_mesh_path), config_dir);
            std::filesystem::path candidate = mesh_path;
            candidate.replace_extension("");
            candidate += "_geometry_config.yaml";
            std::cout << "[geometry resolve] mesh.path-derived candidate='"
                      << candidate.string() << "'"
                      << " exists=" << (std::filesystem::exists(candidate) ? "true" : "false")
                      << "\n";
            if (std::filesystem::exists(candidate)) {
                GeometryConfigResolution out;
                out.path = candidate;
                out.explicit_path = false;
                out.source = "mesh.path-derived";
                return out;
            }
        }
    }

    // 3) Locate from geometry name in XEMFEM_geometry directories near executable
    const std::string geometry_name = extract_geometry_name(root);
    if (!geometry_name.empty()) {
        std::cout << "[geometry resolve] searching by geometry name='"
                  << geometry_name << "'\n";
    }
    const auto search_bases = build_geometry_search_bases(root, exe_dir_opt, config_dir);
    if (!search_bases.empty()) {
        std::cout << "[geometry resolve] base dirs:";
        for (const auto &base : search_bases) {
            std::cout << " " << base.string();
        }
        std::cout << "\n";
    }
    if (const auto by_name = resolve_geometry_config_from_geometry_name(root, exe_dir_opt, config_dir)) {
        GeometryConfigResolution out;
        out.path = *by_name;
        out.explicit_path = false;
        out.source = "geometry-name-search";
        return out;
    }

    std::cout << "[geometry resolve] no geometry config resolved\n";
    return std::nullopt;
}

static std::optional<std::filesystem::path>
resolve_mesh_path_from_geometry_config(const YAML::Node &geom_root,
                                       const std::filesystem::path &geometry_config_path)
{
    namespace fs = std::filesystem;
    const fs::path gc_dir = geometry_config_path.parent_path();

    auto resolve_existing_file = [](const fs::path &candidate) -> std::optional<fs::path> {
        if (candidate.empty()) return std::nullopt;
        std::error_code ec;
        if (!fs::exists(candidate, ec) || ec) return std::nullopt;
        if (!fs::is_regular_file(candidate, ec) || ec) return std::nullopt;
        return fs::absolute(candidate);
    };

    // Primary source: geometry config generation.output_mesh
    const YAML::Node generation = geom_root["generation"];
    if (generation && generation.IsMap() && generation["output_mesh"]) {
        const std::string raw = generation["output_mesh"].as<std::string>("");
        if (!raw.empty()) {
            fs::path mesh_path(raw);
            if (mesh_path.is_relative()) {
                mesh_path = gc_dir / mesh_path;
            }
            if (const auto found = resolve_existing_file(mesh_path)) {
                return found;
            }
        }
    }

    // Fallback: derive from geometry config filename.
    // e.g. slice_022.50deg_gmsh_geometry_config.yaml -> slice_022.50deg_gmsh.msh
    std::string stem = geometry_config_path.stem().string();
    constexpr const char *suffix = "_geometry_config";
    if (ends_with(stem, suffix)) {
        stem.erase(stem.size() - std::strlen(suffix));
    }
    fs::path derived = gc_dir / (stem + ".msh");
    if (const auto found = resolve_existing_file(derived)) {
        return found;
    }

    return std::nullopt;
}

static void merge_geometry_mapping(YAML::Node &root, const YAML::Node &geom_root)
{
    if (!geom_root || !geom_root.IsMap()) {
        throw std::runtime_error("Geometry config is not a YAML map.");
    }

    // ---- materials: copy attr_id from geometry config, preserve user epsilon_r
    const YAML::Node geom_materials = geom_root["materials"];
    if (geom_materials && geom_materials.IsMap()) {
        if (!root["materials"] || !root["materials"].IsMap()) {
            root["materials"] = YAML::Node(YAML::NodeType::Map);
        }
        YAML::Node materials = root["materials"];

        for (auto it = geom_materials.begin(); it != geom_materials.end(); ++it) {
            const std::string name = it->first.as<std::string>();
            const YAML::Node src = it->second;
            if (!src || !src.IsMap()) continue;

            if (!materials[name] || !materials[name].IsMap()) {
                materials[name] = YAML::Node(YAML::NodeType::Map);
            }
            YAML::Node dst = materials[name];

            if (src["attr_id"]) {
                dst["attr_id"] = src["attr_id"].as<int>();
            }
        }
    }

    // ---- boundaries: copy bdr_id/type/value from geometry config, preserve user overrides
    const YAML::Node geom_boundaries = geom_root["boundaries"];
    if (geom_boundaries && geom_boundaries.IsMap()) {
        if (!root["boundaries"] || !root["boundaries"].IsMap()) {
            root["boundaries"] = YAML::Node(YAML::NodeType::Map);
        }
        YAML::Node boundaries = root["boundaries"];

        for (auto it = geom_boundaries.begin(); it != geom_boundaries.end(); ++it) {
            const std::string name = it->first.as<std::string>();
            const YAML::Node src = it->second;
            if (!src || !src.IsMap()) continue;

            if (!boundaries[name] || !boundaries[name].IsMap()) {
                boundaries[name] = YAML::Node(YAML::NodeType::Map);
            }
            YAML::Node dst = boundaries[name];

            if (src["bdr_id"]) {
                dst["bdr_id"] = src["bdr_id"].as<int>();
            }
            if (!dst["type"]) {
                if (src["type"]) {
                    dst["type"] = src["type"].as<std::string>();
                } else {
                    dst["type"] = "dirichlet";
                }
            }
            if (!dst["value"] && src["value"]) {
                dst["value"] = src["value"].as<double>();
            }
        }
    }

    // ---- fieldcage network: fill missing keys from geometry config unless disabled
    bool merge_geometry_fieldcage_network = true;
    const YAML::Node mesh = root["mesh"];
    if (mesh && mesh.IsMap()) {
        if (mesh["fieldcage_network_from_geometry"]) {
            merge_geometry_fieldcage_network =
                mesh["fieldcage_network_from_geometry"].as<bool>(
                    merge_geometry_fieldcage_network);
        } else if (mesh["merge_geometry_fieldcage_network"]) {
            merge_geometry_fieldcage_network =
                mesh["merge_geometry_fieldcage_network"].as<bool>(
                    merge_geometry_fieldcage_network);
        }
    }
    if (merge_geometry_fieldcage_network) {
        const YAML::Node geom_fc = geom_root["fieldcage_network"];
        if (geom_fc && geom_fc.IsMap()) {
            if (!root["fieldcage_network"] || !root["fieldcage_network"].IsMap()) {
                root["fieldcage_network"] = geom_fc;
            } else {
                YAML::Node fc = root["fieldcage_network"];
                for (auto it = geom_fc.begin(); it != geom_fc.end(); ++it) {
                    const std::string key = it->first.as<std::string>();
                    if (!fc[key]) {
                        fc[key] = it->second;
                    }
                }
            }
        }
    }
}

static YAML::Node get_voltage_cfg_node(YAML::Node &root)
{
    YAML::Node vcfg = root["voltage_config"];
    if (!vcfg) {
        vcfg = root["voltage"]; // compatibility alias
    }
    return vcfg;
}

static std::string canonicalize_boundary_key(const std::string &name)
{
    std::string norm;
    norm.reserve(name.size());
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            norm.push_back(static_cast<char>(std::toupper(c)));
        }
    }

    // Ignore optional BC prefix to make naming resilient to BC_/case/underscore variants.
    if (norm.rfind("BC", 0) == 0) {
        norm.erase(0, 2);
    }
    return norm;
}

static std::string alias_from_boundary_name(const std::string &name)
{
    std::string alias = canonicalize_boundary_key(name);
    std::transform(alias.begin(), alias.end(), alias.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return alias;
}

static std::string join_list(const std::vector<std::string> &items,
                             const std::string &sep = ", ")
{
    std::ostringstream oss;
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) oss << sep;
        oss << items[i];
    }
    return oss.str();
}

static bool voltage_values_requests_alias(const YAML::Node &root,
                                          const std::string &alias_norm)
{
    if (alias_norm.empty()) return false;
    YAML::Node vcfg = root["voltage_config"];
    if (!vcfg) {
        vcfg = root["voltage"]; // compatibility alias
    }
    if (!vcfg || !vcfg.IsMap()) return false;

    const YAML::Node values = vcfg["values"];
    if (!values || !values.IsMap()) return false;

    for (auto it = values.begin(); it != values.end(); ++it) {
        const std::string key = it->first.as<std::string>("");
        if (canonicalize_boundary_key(key) == alias_norm) {
            return true;
        }
    }
    return false;
}

static bool has_nonempty_boundary_group(const YAML::Node &boundaries,
                                        const std::string &name)
{
    if (!boundaries || !boundaries.IsMap()) return false;
    const YAML::Node b = boundaries[name];
    return (b && b.IsMap());
}

static std::optional<std::string>
resolve_boundary_name_best_effort(const std::string &raw_key,
                                  const YAML::Node &boundaries,
                                  std::string &error_msg)
{
    YAML::Node exact = boundaries[raw_key];
    if (exact && exact.IsMap()) {
        return raw_key;
    }

    const std::string raw_norm = canonicalize_boundary_key(raw_key);
    if (raw_norm.empty()) {
        error_msg = "voltage_config.values key '" + raw_key +
                    "' is empty after normalization.";
        return std::nullopt;
    }

    std::vector<std::string> norm_match;
    std::vector<std::string> fuzzy_match;

    for (auto it = boundaries.begin(); it != boundaries.end(); ++it) {
        const std::string bname = it->first.as<std::string>();
        const std::string bnorm = canonicalize_boundary_key(bname);
        if (bnorm == raw_norm) {
            norm_match.push_back(bname);
            continue;
        }

        if (!bnorm.empty() &&
            (bnorm.find(raw_norm) != std::string::npos ||
             raw_norm.find(bnorm) != std::string::npos)) {
            fuzzy_match.push_back(bname);
        }
    }

    if (norm_match.size() == 1) {
        return norm_match.front();
    }
    if (norm_match.size() > 1) {
        std::sort(norm_match.begin(), norm_match.end());
        error_msg = "voltage_config.values key '" + raw_key +
                    "' is ambiguous after normalization; candidates: " +
                    join_list(norm_match);
        return std::nullopt;
    }

    if (fuzzy_match.size() == 1) {
        return fuzzy_match.front();
    }
    if (fuzzy_match.size() > 1) {
        std::sort(fuzzy_match.begin(), fuzzy_match.end());
        error_msg = "voltage_config.values key '" + raw_key +
                    "' is ambiguous in fuzzy matching; candidates: " +
                    join_list(fuzzy_match);
        return std::nullopt;
    }

    if (raw_norm == "AXIS") {
        error_msg =
            "voltage_config.values key '" + raw_key +
            "' did not match any boundary. Expected boundary group 'BC_Axis' is missing "
            "from loaded geometry config.";
        return std::nullopt;
    }
    if (raw_norm == "WALLCHARGE") {
        error_msg =
            "voltage_config.values key '" + raw_key +
            "' did not match any boundary. Expected boundary group 'BC_WallCharge' is missing "
            "from loaded geometry config.";
        return std::nullopt;
    }

    error_msg = "voltage_config.values key '" + raw_key +
                "' did not match any boundary (normalized as '" + raw_norm + "').";
    return std::nullopt;
}

static VoltageApplyResult apply_voltage_values(YAML::Node &root)
{
    VoltageApplyResult result;
    YAML::Node vcfg = get_voltage_cfg_node(root);
    if (!vcfg) {
        return result;
    }
    if (!vcfg.IsMap()) {
        throw std::runtime_error("voltage_config must be a mapping.");
    }

    const YAML::Node values = vcfg["values"];
    if (!values) {
        return result;
    }
    if (!values.IsMap()) {
        throw std::runtime_error("voltage_config.values must be a mapping of boundary -> voltage.");
    }

    YAML::Node boundaries = root["boundaries"];
    if (!boundaries || !boundaries.IsMap()) {
        throw std::runtime_error(
            "voltage_config.values provided but boundaries mapping is missing. "
            "Provide mesh.geometry_config or boundaries.");
    }

    std::vector<std::string> unresolved_errors;
    std::map<std::string, std::string> matched_by_key;

    for (auto it = values.begin(); it != values.end(); ++it) {
        const std::string raw_key = it->first.as<std::string>();
        double v = 0.0;
        try {
            v = it->second.as<double>();
        } catch (...) {
            throw std::runtime_error(
                "voltage_config.values['" + raw_key + "'] must be numeric.");
        }

        std::string resolve_error;
        const auto resolved_name =
            resolve_boundary_name_best_effort(raw_key, boundaries, resolve_error);
        if (!resolved_name) {
            unresolved_errors.push_back(resolve_error);
            continue;
        }

        const auto prev_it = matched_by_key.find(*resolved_name);
        if (prev_it != matched_by_key.end()) {
            unresolved_errors.push_back(
                "voltage_config.values has multiple entries resolving to boundary '" +
                *resolved_name + "': '" + prev_it->second + "' and '" + raw_key + "'.");
            continue;
        }
        matched_by_key[*resolved_name] = raw_key;

        YAML::Node bnd = boundaries[*resolved_name];
        if (!bnd || !bnd.IsMap()) {
            throw std::runtime_error("Internal error: resolved boundary '" +
                                     *resolved_name + "' is not a mapping.");
        }

        if (!bnd["type"]) {
            bnd["type"] = "dirichlet";
        }
        bnd["value"] = v;
        result.matched_boundaries.insert(*resolved_name);
    }

    if (!unresolved_errors.empty()) {
        std::sort(unresolved_errors.begin(), unresolved_errors.end());
        std::ostringstream oss;
        oss << "voltage_config.values matching failed:";
        for (const auto &e : unresolved_errors) {
            oss << "\n  - " << e;
        }
        throw std::runtime_error(oss.str());
    }

    return result;
}

static std::set<std::string>
collect_fieldcage_auto_assigned_boundaries(const YAML::Node &root)
{
    std::set<std::string> auto_assigned;
    const YAML::Node fc = root["fieldcage_network"];
    if (!fc || !fc.IsMap()) {
        return auto_assigned;
    }

    const bool fc_enabled = fc["enabled"] ? fc["enabled"].as<bool>(true) : true;
    if (!fc_enabled) {
        return auto_assigned;
    }

    const YAML::Node nodes = fc["nodes"];
    if (!nodes || !nodes.IsSequence()) {
        return auto_assigned;
    }

    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const YAML::Node nd = nodes[i];
        if (!nd || !nd.IsMap()) continue;
        const bool fixed = nd["fixed"] ? nd["fixed"].as<bool>(false) : false;
        if (fixed) continue;
        const std::string bname = nd["boundary"] ? nd["boundary"].as<std::string>("") : "";
        if (!bname.empty()) {
            auto_assigned.insert(bname);
        }
    }

    return auto_assigned;
}

static void enforce_required_boundary_values(const YAML::Node &root,
                                             const std::set<std::string> &matched_boundaries)
{
    const YAML::Node boundaries = root["boundaries"];
    if (!boundaries || !boundaries.IsMap()) {
        throw std::runtime_error(
            "voltage validation requires boundaries mapping, but none was found.");
    }

    const std::set<std::string> auto_assigned =
        collect_fieldcage_auto_assigned_boundaries(root);

    std::vector<std::string> missing;
    auto has_finite_scalar_value = [](const YAML::Node &bnd) {
        if (!bnd || !bnd.IsMap() || !bnd["value"]) {
            return false;
        }
        try {
            const double v = bnd["value"].as<double>();
            return std::isfinite(v);
        } catch (...) {
            return false;
        }
    };

    for (auto it = boundaries.begin(); it != boundaries.end(); ++it) {
        const std::string bname = it->first.as<std::string>();
        const YAML::Node bnd = it->second;
        if (!bnd || !bnd.IsMap()) continue;

        std::string btype = bnd["type"] ? bnd["type"].as<std::string>("dirichlet")
                                        : "dirichlet";
        std::transform(btype.begin(), btype.end(), btype.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        const bool needs_scalar_value = (btype == "dirichlet" || btype == "neumann");
        if (!needs_scalar_value) continue;

        // Non-fixed field-cage nodes are solved by resistor chain.
        const bool auto_fieldcage = auto_assigned.count(bname) > 0;
        const bool has_scalar_value = has_finite_scalar_value(bnd);
        if (auto_fieldcage) {
            if (matched_boundaries.count(bname) || has_scalar_value) {
                missing.push_back(
                    bname +
                    " (should not be set explicitly; solved by resistor chain)");
            }
            continue;
        }

        if (!matched_boundaries.count(bname) && !has_scalar_value) {
            const std::string alias = alias_from_boundary_name(bname);
            if (alias.empty()) {
                missing.push_back(bname);
            } else {
                missing.push_back(bname + " (alias: " + alias + ")");
            }
        }
    }

    if (!missing.empty()) {
        std::sort(missing.begin(), missing.end());
        std::ostringstream oss;
        oss << "Missing or invalid explicit values for required boundaries: "
            << join_list(missing)
            << ". Provide one voltage_config.values entry per required boundary. "
            << "Name matching is best-effort and ignores BC-prefix, underscores, and case.";
        throw std::runtime_error(oss.str());
    }
}

static void enforce_required_fixed_fieldcage_voltages(const YAML::Node &root)
{
    YAML::Node vcfg = root["voltage_config"];
    if (!vcfg) {
        vcfg = root["voltage"]; // compatibility alias
    }

    bool require_fixed = true;
    if (vcfg && vcfg.IsMap() && vcfg["require_fixed_fieldcage_values"]) {
        require_fixed = vcfg["require_fixed_fieldcage_values"].as<bool>(require_fixed);
    }
    if (!require_fixed) {
        return;
    }

    const YAML::Node fc = root["fieldcage_network"];
    if (!fc || !fc.IsMap()) {
        return;
    }

    const bool fc_enabled = fc["enabled"] ? fc["enabled"].as<bool>(true) : true;
    if (!fc_enabled) {
        return;
    }

    const YAML::Node nodes = fc["nodes"];
    if (!nodes || !nodes.IsSequence()) {
        return;
    }

    const YAML::Node boundaries = root["boundaries"];
    if (!boundaries || !boundaries.IsMap()) {
        throw std::runtime_error(
            "fieldcage_network is enabled but boundaries mapping is missing.");
    }

    std::set<std::string> missing;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const YAML::Node nd = nodes[i];
        if (!nd || !nd.IsMap()) continue;

        const bool fixed = nd["fixed"] ? nd["fixed"].as<bool>(false) : false;
        if (!fixed) continue;

        const std::string bname = nd["boundary"] ? nd["boundary"].as<std::string>("") : "";
        if (bname.empty()) continue;

        const YAML::Node bnd = boundaries[bname];
        if (!bnd || !bnd.IsMap() || !bnd["value"]) {
            missing.insert(bname);
        }
    }

    // Optional explicit additional required boundaries
    if (vcfg && vcfg.IsMap() && vcfg["required_boundaries"]) {
        const YAML::Node req = vcfg["required_boundaries"];
        if (!req.IsSequence()) {
            throw std::runtime_error("voltage_config.required_boundaries must be a sequence.");
        }
        for (std::size_t i = 0; i < req.size(); ++i) {
            const std::string bname = req[i].as<std::string>("");
            if (bname.empty()) continue;
            const YAML::Node bnd = boundaries[bname];
            if (!bnd || !bnd.IsMap() || !bnd["value"]) {
                missing.insert(bname);
            }
        }
    }

    if (!missing.empty()) {
        std::ostringstream oss;
        oss << "Missing voltage assignments for required boundaries: ";
        bool first = true;
        for (const auto &name : missing) {
            if (!first) oss << ", ";
            oss << name;
            first = false;
        }
        throw std::runtime_error(oss.str());
    }
}

static void hydrate_config_document_for_runtime(ConfigDocument &config_doc,
                                                const std::filesystem::path &config_path,
                                                const std::optional<std::filesystem::path> &exe_dir_opt)
{
    YAML::Node &root = config_doc.Root();
    const std::filesystem::path config_dir =
        std::filesystem::absolute(config_path).parent_path();
    std::cout << "[geometry resolve] config dir='" << config_dir.string() << "'\n";

    // Primary mode for geometry-driven workflows: generate mesh+geometry-config
    // from source geometry into save_path/mesh when no explicit mesh/config path
    // is supplied.
    maybe_generate_geometry_from_source(config_doc, config_dir, exe_dir_opt);

    const auto resolved = resolve_geometry_config_path(root, config_dir, exe_dir_opt);
    if (resolved) {
        const std::filesystem::path gc_path = std::filesystem::absolute(resolved->path);
        std::cout << "[geometry resolve] selected source='" << resolved->source
                  << "' path='" << gc_path.string() << "'"
                  << " exists=" << (std::filesystem::exists(gc_path) ? "true" : "false")
                  << "\n";
        if (!std::filesystem::exists(gc_path)) {
            if (resolved->explicit_path) {
                throw std::runtime_error(
                    "mesh.geometry_config path does not exist: " + gc_path.string());
            }
        } else {
            YAML::Node geom_root = YAML::LoadFile(gc_path.string());
            merge_geometry_mapping(root, geom_root);
            config_doc.SetPath("mesh.geometry_config", YAML::Node(gc_path.string()), true);

            const YAML::Node boundaries = root["boundaries"];
            std::vector<std::string> missing_from_geom_cfg;
            if (voltage_values_requests_alias(root, "AXIS") &&
                !has_nonempty_boundary_group(boundaries, "BC_Axis")) {
                missing_from_geom_cfg.push_back("BC_Axis");
            }
            if (voltage_values_requests_alias(root, "WALLCHARGE") &&
                !has_nonempty_boundary_group(boundaries, "BC_WallCharge")) {
                missing_from_geom_cfg.push_back("BC_WallCharge");
            }
            if (!missing_from_geom_cfg.empty()) {
                std::ostringstream oss;
                oss << "Loaded geometry config '" << gc_path.string()
                    << "' is missing required boundary group(s): "
                    << join_list(missing_from_geom_cfg)
                    << ". These are requested by voltage_config.values (Axis/WallCharge). "
                    << "This runtime path validates an existing mesh/config; it does not regenerate mesh geometry. "
                    << "Regenerate the mesh + *_geometry_config.yaml with the updated mesher, then rerun.";
                throw std::runtime_error(oss.str());
            }

            YAML::Node mesh = root["mesh"];
            const bool has_mesh_path =
                mesh && mesh.IsMap() && mesh["path"] &&
                !mesh["path"].as<std::string>("").empty();
            if (!has_mesh_path) {
                if (const auto mesh_path = resolve_mesh_path_from_geometry_config(geom_root, gc_path)) {
                    config_doc.SetPath("mesh.path", YAML::Node(mesh_path->string()), true);
                } else {
                    std::ostringstream oss;
                    oss << "Could not resolve mesh.path from geometry config: "
                        << gc_path.string()
                        << ". Set mesh.path explicitly or ensure generation.output_mesh points to an existing .msh.";
                    throw std::runtime_error(oss.str());
                }
            }
        }
    }
    if (!resolved) {
        const YAML::Node boundaries = root["boundaries"];
        const YAML::Node vcfg = get_voltage_cfg_node(root);
        const bool needs_boundary_map =
            vcfg && vcfg.IsMap() && vcfg["values"] && (!boundaries || !boundaries.IsMap());

        if (needs_boundary_map) {
            std::string geometry_name;
            const YAML::Node mesh = root["mesh"];
            if (mesh && mesh.IsMap() && mesh["geometry"]) {
                geometry_name = mesh["geometry"].as<std::string>("");
            }
            if (geometry_name.empty() && root["geometry_id"]) {
                geometry_name = root["geometry_id"].as<std::string>("");
            }

            std::ostringstream oss;
            oss << "Could not resolve geometry boundary mapping. "
                << "Provide mesh.geometry_config or ensure a geometry config exists for geometry '"
                << geometry_name << "' in XEMFEM_geometry/XEMFEM_geometries search roots.";
            oss << " Note: this runtime path does not remesh geometry.";
            const auto search_bases =
                build_geometry_search_bases(root, exe_dir_opt, config_dir);
            if (!search_bases.empty()) {
                oss << " Searched base dirs in order: ";
                for (std::size_t i = 0; i < search_bases.size(); ++i) {
                    if (i) oss << ", ";
                    oss << search_bases[i].string();
                }
                oss << " (each with ., geometry, mesh, geometry/mesh, XEMFEM_geometry, "
                    << "XEMFEM_geometries, geometry/XEMFEM_geometry, "
                    << "geometry/XEMFEM_geometries).";
            }
            oss << " Optional override: mesh.geometry_config_search_root.";
            throw std::runtime_error(oss.str());
        }
    }

    const VoltageApplyResult voltage_apply = apply_voltage_values(root);
    enforce_required_boundary_values(root, voltage_apply.matched_boundaries);
    enforce_required_fixed_fieldcage_voltages(root);
}

} // namespace

static int run_sim(Config init_cfg, ConfigDocument config_doc) {
    const auto &sweeps = init_cfg.sweeps;
    bool used_precomputed_amr_mesh = false;

    auto sweeps_touch_mesh_inputs = [](const std::vector<SweepEntry> &entries) {
        auto path_touches_mesh = [](const std::string &path) {
            return !path.empty() && path.rfind("mesh.", 0) == 0;
        };
        for (const auto &sw : entries) {
            if ((sw.kind == SweepEntry::Kind::Discrete ||
                 sw.kind == SweepEntry::Kind::Range) &&
                path_touches_mesh(sw.path)) {
                return true;
            }
            if (sw.kind == SweepEntry::Kind::Fixed) {
                for (const auto &cfg : sw.configs) {
                    for (const auto &a : cfg.assigns) {
                        if (path_touches_mesh(a.path)) return true;
                    }
                }
            }
        }
        return false;
    };
    const bool sweep_mode = !sweeps.empty();
    const bool sweeps_modify_mesh_inputs =
        sweep_mode && sweeps_touch_mesh_inputs(sweeps);

    if (init_cfg.mesh.amr.enable && (!sweep_mode || !sweeps_modify_mesh_inputs))
    {
        std::cout << "[MAIN] Producing AMR Mesh" << std::endl;
        std::string mesh_path = PrecomputeAMRMesh(init_cfg);
        init_cfg.mesh.path = mesh_path;
        config_doc.SetPath("mesh.path", YAML::Node(mesh_path), true);
        used_precomputed_amr_mesh = true;
    }
    else if (init_cfg.mesh.amr.enable && sweep_mode && sweeps_modify_mesh_inputs)
    {
        std::cout << "[MAIN] AMR precompute skipped: sweep modifies mesh inputs; "
                     "AMR will run per sweep point" << std::endl;
    }

    // Do optimization
    if (init_cfg.optimize.enabled && (!init_cfg.optimize.metrics_only))
    {
        std::cout << "[MAIN] Executing an optimization" << std::endl;
        run_optimization(init_cfg, config_doc);
        return 0;
    }
    // Do sweep
    else if (!sweeps.empty())
    {
        std::cout << "[MAIN] Executing a sweep" << std::endl;
        std::size_t run_counter = 0;
        std::vector<std::pair<std::string, std::string>> active_params;
        std::vector<RunRecord> records;
        std::vector<Assignment> assignments;

        sweep_recursive_cfg(init_cfg,
                            config_doc, 
                            sweeps,
                            /* idx        */ 0,
                            active_params,
                            assignments,
                            used_precomputed_amr_mesh,
                            run_counter,
                            records);

        std::filesystem::path save_root(init_cfg.save_path);
        write_sweep_meta(init_cfg.geometry_id, save_root, records);
        return 0;
    }
    // Single Run     
    else
    {
        std::cout << "[MAIN] Executing a simulation" << std::endl;
        std::size_t run_counter = 0;
        std::vector<std::pair<std::string, std::string>> active_params;
        std::vector<RunRecord> records;
        // Single run uses the same run_one + RunRecord machinery as a 1-point sweep
        YAML::Node root = config_doc.Root();
        const bool skip_amr_runtime = used_precomputed_amr_mesh;
        std::string run_dir_name = run_one(init_cfg, root, active_params, run_counter, skip_amr_runtime);
        if (!run_dir_name.empty())  
        {
            RunRecord rec;
            rec.run_dir_name = run_dir_name;
            rec.params       = active_params; // empty -> (none)
            records.push_back(std::move(rec));
        }
        ++run_counter; // used only for naming run_0001

        std::filesystem::path save_root(init_cfg.save_path);
        write_sweep_meta(init_cfg.geometry_id, save_root, records);
        return 0;
    }
    return 0;
}

static int run_metrics(Config init_cfg) {
    run_metrics_only(init_cfg);
    return 0;
}

static int run_plot(Config init_cfg) {
    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#if HAVE_VTK
    if (rank == 0) {
        return make_plot_api(init_cfg);
    }
    MPI_Barrier(MPI_COMM_WORLD);
    return 0;
#else
    if (rank == 0) {
        std::cerr << "Plotting is not available in this build. "
                  << "Reconfigure with -DXEMFEM_ENABLE_VTK=ON and rebuild.\n";
    }
    MPI_Barrier(MPI_COMM_WORLD);
    return 0;
#endif
}

static int run_interpolate(Config init_cfg) {
    do_interpolate(init_cfg);
    return 0;
}

static std::optional<std::filesystem::path> detect_executable_dir(const char *argv0)
{
    namespace fs = std::filesystem;
#ifdef __linux__
    try {
        const fs::path p = fs::read_symlink("/proc/self/exe");
        if (!p.empty()) {
            return fs::absolute(p).parent_path();
        }
    } catch (...) {
    }
#endif
    if (argv0 && *argv0) {
        try {
            fs::path p(argv0);
            if (p.is_relative()) {
                p = fs::absolute(p);
            }
            return p.parent_path();
        } catch (...) {
        }
    }
    return std::nullopt;
}

int main(int argc, char** argv)
{    
    // --------------------- MPI needs to be innited early ----------------------------
    parallel::init_mpi(argc, argv);

    int rank = 0;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    int world_size = 1;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);
    // -------------------- Check Subcommand ----------------------
    cli::InputParser pre_args(argc, argv);

    if (pre_args.has("-h") || pre_args.has("--help")) {
        cli::print_usage(argv[0]);
        return 0;
    }

    const std::string cmd = pre_args.subcommand().value_or("sim");

    // Strip subcommand 
    cli::InputParser::strip_subcommand(argc, argv);

    // ---------------------- Read options -----------------------------
    cli::InputParser args(argc, argv);

    std::string config_path_str;
    int config_ok = 1;

    if (rank == 0) {
        auto config_str_opt = args.get("-c");
        if (!config_str_opt) { config_str_opt = args.get("--config"); }

        std::filesystem::path config_path;
        if (!config_str_opt) {
            std::cout << "Using Default config path ../geometry/config.yaml \n";
            config_path = cli::to_absolute("../geometry/config.yaml");
        } else {
            config_path = cli::to_absolute(*config_str_opt);
        }

        config_path_str = config_path.string();

        if (!std::filesystem::exists(config_path)) {
            std::cout << "Error: config file not found: " << config_path << "\n";
            config_ok = 0;
        } else {
            std::cout << "[Config] " << config_path << "\n";
        }
    }

    // Broadcast config_ok
    MPI_Bcast(&config_ok, 1, MPI_INT, 0, MPI_COMM_WORLD);
    if (!config_ok) {
        return 1;
    }

    // Broadcast config path string
    std::uint64_t path_n = 0;
    if (rank == 0) path_n = static_cast<std::uint64_t>(config_path_str.size());
    MPI_Bcast(&path_n, 1, MPI_UINT64_T, 0, MPI_COMM_WORLD);

    config_path_str.resize(static_cast<std::size_t>(path_n));
    if (path_n > 0) {
        MPI_Bcast(config_path_str.data(), static_cast<int>(path_n), MPI_CHAR, 0, MPI_COMM_WORLD);
    }

    // ---------------------- Load Config ------------------------------
    const auto exe_dir_opt = detect_executable_dir(argv[0]);
    ConfigDocument config_doc = ConfigDocument::Load(config_path_str, MPI_COMM_WORLD);
    maybe_expand_geometry_sweep(
        config_doc,
        std::filesystem::absolute(std::filesystem::path(config_path_str)).parent_path(),
        exe_dir_opt);
    hydrate_config_document_for_runtime(
        config_doc,
        std::filesystem::path(config_path_str),
        exe_dir_opt);
    Config init_cfg = config_doc.ToConfig(cmd);
    // MPI Set Up
    parallel::init_environment(init_cfg);

    // Mark the number of ranks used (persisted into run-local config snapshot).
    config_doc.SetPath("mpi.ranks", YAML::Node(world_size), true);
    // ------------------------- Dispatch ------------------------------
    if (cmd == "sim") {
        ensure_directory(init_cfg);
        run_sim(init_cfg, config_doc);
        std::cout << "Done" <<std::endl;
        return 0;
    }
    if (cmd == "metrics") {
        return run_metrics(init_cfg);
        std::cout << "Done" <<std::endl;
        return 0;
    }
    if (cmd == "plot") {
        // TODO Needs extra args?
        if (rank == 0)
            run_plot(init_cfg);
            std::cout << "Done" <<std::endl;
        return 0;
    }
    if (cmd == "interpolate") {
        // TODO Needs extra args?
        run_interpolate(init_cfg);
        std::cout << "Done" <<std::endl;
        return 0;
    }

    std::cerr << "Error: unknown subcommand '" << cmd << "'\n";
    cli::print_usage(argv[0]);
    return 1;
}
