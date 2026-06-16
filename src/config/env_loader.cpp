#include "env_loader.hpp"

#include <cstdlib>
#include <fstream>
#include <string>

namespace {

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string unquote(std::string value) {
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') ||
         (value.front() == '\'' && value.back() == '\''))) {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

void set_env_if_missing(const std::string& key, const std::string& value) {
    if (std::getenv(key.c_str()) != nullptr) {
        return;
    }

#ifdef _WIN32
    _putenv_s(key.c_str(), value.c_str());
#else
    setenv(key.c_str(), value.c_str(), 0);
#endif
}

} // namespace

void load_env_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }

        const std::string key = trim(line.substr(0, equals));
        const std::string value = unquote(trim(line.substr(equals + 1)));
        if (!key.empty()) {
            set_env_if_missing(key, value);
        }
    }
}
