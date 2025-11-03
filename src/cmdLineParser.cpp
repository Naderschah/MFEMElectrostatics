#include "InputParser.hpp"

/*
https://stackoverflow.com/questions/865668/parsing-command-line-arguments-in-c
*/


namespace cli {

InputParser::InputParser(int& argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        tokens.emplace_back(argv[i]);
    }
}

bool InputParser::has(const std::string& opt) const {
    return std::find(tokens.begin(), tokens.end(), opt) != tokens.end();
}

std::optional<std::string> InputParser::get(const std::string& opt) const {
    auto it = std::find(tokens.begin(), tokens.end(), opt);
    if (it != tokens.end() && ++it != tokens.end())
        return *it;
    return std::nullopt;
}

std::filesystem::path to_absolute(const std::filesystem::path& p) {
    using fs = std::filesystem;
    fs::path abs = p.is_absolute() ? p : fs::absolute(p);
    std::error_code ec;
    fs::path norm = fs::weakly_canonical(abs, ec);
    return ec ? abs : norm;
}

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " [-c <config.yaml>] [-m <model>] [--help]\n"
        << "  -c, --config   Path to YAML config (default: config/config.yaml)\n"
        << "  -m, --model    Path to model/resource\n"
        << "  -h, --help     Show this help\n";
}

} // namespace cli
