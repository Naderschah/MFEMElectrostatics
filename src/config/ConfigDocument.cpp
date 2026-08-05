#include "ConfigDocument.h"

#include <stdexcept>

ConfigDocument ConfigDocument::Load(const std::string &path, MPI_Comm comm)
{
    const std::string yaml_str = ReadConfigString(path, comm);
    return FromString(yaml_str);
}

ConfigDocument ConfigDocument::FromString(const std::string &yaml_str)
{
    YAML::Node root = YAML::Load(yaml_str);
    return FromNode(root);
}

ConfigDocument ConfigDocument::FromNode(const YAML::Node &root)
{
    YAML::Node copy = root;
    if (copy && !copy.IsMap())
    {
        throw std::runtime_error("ConfigDocument root must be a YAML map.");
    }
    return ConfigDocument(copy);
}

std::string ConfigDocument::ToString() const
{
    YAML::Emitter out;
    out << root_;
    return std::string(out.c_str());
}

Config ConfigDocument::ToConfig() const
{
    return Config::LoadFromNode(root_);
}

Config ConfigDocument::ToConfig(const std::string &run_mode) const
{
    return Config::LoadFromNode(root_, run_mode);
}

std::vector<std::string> ConfigDocument::SplitPath(const std::string &dot_path)
{
    std::vector<std::string> parts;
    std::string current;
    current.reserve(dot_path.size());

    for (char c : dot_path)
    {
        if (c == '.')
        {
            if (!current.empty())
            {
                parts.push_back(current);
                current.clear();
            }
        }
        else
        {
            current.push_back(c);
        }
    }

    if (!current.empty())
    {
        parts.push_back(current);
    }

    return parts;
}

bool ConfigDocument::HasPath(const std::string &dot_path) const
{
    if (!root_)
    {
        return false;
    }

    const auto parts = SplitPath(dot_path);
    if (parts.empty())
    {
        return false;
    }

    YAML::Node cur = root_;
    for (const auto &key : parts)
    {
        if (!cur || !cur.IsMap())
        {
            return false;
        }
        cur.reset(cur[key]);
        if (!cur)
        {
            return false;
        }
    }
    return true;
}

YAML::Node ConfigDocument::GetPath(const std::string &dot_path) const
{
    if (!root_)
    {
        return YAML::Node();
    }

    const auto parts = SplitPath(dot_path);
    if (parts.empty())
    {
        return YAML::Node();
    }

    YAML::Node cur = root_;
    for (const auto &key : parts)
    {
        if (!cur || !cur.IsMap())
        {
            return YAML::Node();
        }
        cur.reset(cur[key]);
        if (!cur)
        {
            return YAML::Node();
        }
    }

    return cur;
}

void ConfigDocument::SetPath(const std::string &dot_path,
                             const YAML::Node &value,
                             bool create_missing)
{
    const auto parts = SplitPath(dot_path);
    if (parts.empty())
    {
        throw std::runtime_error("ConfigDocument::SetPath received an empty path.");
    }

    if (!root_)
    {
        if (!create_missing)
        {
            throw std::runtime_error(
                "ConfigDocument::SetPath cannot set path on an empty document.");
        }
        root_ = YAML::Node(YAML::NodeType::Map);
    }

    if (!root_.IsMap())
    {
        throw std::runtime_error("ConfigDocument root is not a YAML map.");
    }

    YAML::Node cur = root_;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i)
    {
        const std::string &key = parts[i];
        YAML::Node next = cur[key];

        if (!next)
        {
            if (!create_missing)
            {
                throw std::runtime_error(
                    "ConfigDocument::SetPath missing key '" + key +
                    "' while creating is disabled.");
            }
            cur[key] = YAML::Node(YAML::NodeType::Map);
            next = cur[key];
        }

        if (!next.IsMap())
        {
            throw std::runtime_error(
                "ConfigDocument::SetPath path traverses non-map key '" + key + "'.");
        }

        cur.reset(next);
    }

    cur[parts.back()] = value;
}
