#pragma once

#include <mpi.h>
#include <yaml-cpp/yaml.h>

#include <string>
#include <vector>

#include "Config.h"

class ConfigDocument
{
public:
    ConfigDocument() = default;

    static ConfigDocument Load(const std::string &path,
                               MPI_Comm comm = MPI_COMM_WORLD);
    static ConfigDocument FromString(const std::string &yaml_str);
    static ConfigDocument FromNode(const YAML::Node &root);

    const YAML::Node &Root() const { return root_; }
    YAML::Node &Root() { return root_; }

    std::string ToString() const;

    Config ToConfig() const;
    Config ToConfig(const std::string &run_mode) const;

    bool HasPath(const std::string &dot_path) const;
    YAML::Node GetPath(const std::string &dot_path) const;
    void SetPath(const std::string &dot_path,
                 const YAML::Node &value,
                 bool create_missing = true);

private:
    explicit ConfigDocument(YAML::Node root)
      : root_(std::move(root))
    {
    }

    static std::vector<std::string> SplitPath(const std::string &dot_path);

    YAML::Node root_;
};

