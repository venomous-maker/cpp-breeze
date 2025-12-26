#pragma once
#include <string>
#include <unordered_map>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <breeze/support/str.hpp>

namespace breeze::support {

class Env {
public:
    static bool load(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            return false;
        }

        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {
            // Remove comments
            auto comment_pos = line.find('#');
            if (comment_pos != std::string::npos) {
                line = line.substr(0, comment_pos);
            }

            // Trim whitespace
            line = trim(line);
            if (line.empty()) continue;

            auto eq_pos = line.find('=');
            if (eq_pos == std::string::npos) continue;

            std::string key = trim(line.substr(0, eq_pos));
            std::string value = trim(line.substr(eq_pos + 1));

            // Remove quotes if present
            if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
                value = value.substr(1, value.size() - 2);
            } else if (value.size() >= 2 && value.front() == '\'' && value.back() == '\'') {
                value = value.substr(1, value.size() - 2);
            }

            setenv(key.c_str(), value.c_str(), 1);
        }

        return true;
    }

    static std::string get(const std::string& key, const std::string& fallback = "") {
        const char* val = std::getenv(key.c_str());
        return val ? std::string(val) : fallback;
    }

private:
    static std::string trim(const std::string& s) {
        auto first = s.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) return "";
        auto last = s.find_last_not_of(" \t\n\r");
        return s.substr(first, (last - first + 1));
    }
};

} // namespace breeze::support
