#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <iostream>

namespace cli {

namespace fs = std::filesystem;

/// Lightweight command line argument parser
/// Example usage:
///     cli::InputParser args(argc, argv);
///     if (args.has("-h") || args.has("--help")) ...
///     auto config = args.get("-c").value_or("default.yaml");
class InputParser {
public:
    InputParser(int& argc, char** argv);

    /// Returns true if a flag (e.g. "-h") is present
    bool has(const std::string& opt) const;

    /// Returns the value following a flag (e.g. "-c config.yaml")
    std::optional<std::string> get(const std::string& opt) const;

private:
    std::vector<std::string> tokens;
};

/// Converts a given path to an absolute, normalized path
fs::path to_absolute(const fs::path& p);

/// Prints standard usage info
void print_usage(const char* prog);

} // namespace cli
