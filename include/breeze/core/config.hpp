// include/breeze/core/config.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <regex>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <stdexcept>

namespace breeze::core {

class Config {
public:
    Config() = default;
    
    explicit Config(const std::filesystem::path& config_path) {
        load_from_path(config_path);
    }
    
    // Load configuration from directory
    void load_from_path(const std::filesystem::path& config_path) {
        if (!std::filesystem::exists(config_path)) {
            return;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(config_path)) {
            if (entry.path().extension() == ".json") {
                load_json_file(entry.path());
            }
        }
    }
    
    // Get with type conversion
    template<typename T>
    T get(const std::string& key, T fallback = T()) const {
        auto it = values_.find(key);
        if (it == values_.end()) {
            return fallback;
        }
        
        try {
            if constexpr (std::is_same_v<T, std::string>) {
                return it->second;
            } else if constexpr (std::is_same_v<T, int>) {
                return std::stoi(it->second);
            } else if constexpr (std::is_same_v<T, bool>) {
                std::string lower = it->second;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                return lower == "true" || lower == "1" || lower == "yes";
            } else if constexpr (std::is_same_v<T, double>) {
                return std::stod(it->second);
            } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
                return split_array(it->second);
            }
        } catch (...) {
            return fallback;
        }
        
        return fallback;
    }
    
    // Array access for nested config (e.g., "database.connections.mysql.host")
    std::string get(const std::string& key, std::string fallback = {}) const {
        auto parts = split_key(key);
        const auto* current_map = &values_;

        for (size_t i = 0; i < parts.size(); ++i) {
            auto it = current_map->find(parts[i]);
            if (it == current_map->end()) {
                return fallback;
            }

            if (i == parts.size() - 1) {
                return it->second;
            }

            // For simplicity, we don't support nested objects in this basic version
            // In a full implementation, we'd store nlohmann::json objects
            return fallback;
        }

        return fallback;
    }
    
    void set(std::string key, std::string value) { 
        values_[std::move(key)] = std::move(value); 
    }
    
    template<typename T>
    void set(const std::string& key, T value) {
        if constexpr (std::is_same_v<T, std::string>) {
            set(key, value);
        } else if constexpr (std::is_convertible_v<T, std::string>) {
            set(key, std::string(value));
        } else if constexpr (std::is_same_v<T, bool>) {
            set(key, value ? "true" : "false");
        } else {
            set(key, std::to_string(value));
        }
    }
    
    bool has(const std::string& key) const { 
        return values_.contains(key); 
    }
    
    // Environment variable helpers
    static std::string env(const std::string& key, std::string fallback = {}) {
        if (const char* val = std::getenv(key.c_str())) {
            return {val};
        }
        return fallback;
    }
    
private:
    void load_json_file(const std::filesystem::path& file_path) {
        try {
            std::ifstream file(file_path);
            if (!file.is_open()) return;
            
            nlohmann::json json;
            file >> json;
            
            std::string filename = file_path.stem().string();
            flatten_json(filename, json);

            // After flattening the JSON, resolve any env(...) expressions in loaded values
            for (auto& kv : values_) {
                kv.second = resolve_env_in_value(kv.second);
            }
        } catch (...) {
            // Silently fail on config errors
        }
    }
    
    void flatten_json(const std::string& prefix, const nlohmann::json& json, 
                     const std::string& current_key = "") {
        if (json.is_object()) {
            for (auto it = json.begin(); it != json.end(); ++it) {
                std::string new_key = current_key.empty() 
                    ? prefix + "." + it.key()
                    : current_key + "." + it.key();
                flatten_json(prefix, it.value(), new_key);
            }
        } else if (json.is_array()) {
            std::string array_str;
            for (const auto& item : json) {
                if (!array_str.empty()) array_str += ",";
                if (item.is_string()) {
                    array_str += item.get<std::string>();
                } else {
                    array_str += item.dump();
                }
            }
            values_[current_key] = array_str;
        } else {
            // Use the raw dumped value; for JSON strings this will include quotes
            values_[current_key] = json.dump();
        }
    }

    // Resolve env(...) expressions inside a configuration value.
    // Supports forms: env(KEY) or env(KEY, default) with optional quotes around arguments.
    // This implementation tokenizes and parses env(...) occurrences, validates syntax and
    // throws std::runtime_error on malformed expressions. If no env(...) occurrences
    // are found the original string is returned unchanged.
    static std::string resolve_env_in_value(const std::string& raw) {
        std::string s = raw;
        // Strip surrounding JSON string quotes if present
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            s = s.substr(1, s.size() - 2);
        }

        auto ci_match_env = [](const std::string &str, size_t pos) -> bool {
            // case-insensitive match for "env(" at pos
            return pos + 4 <= str.size()
                && (std::tolower(static_cast<unsigned char>(str[pos])) == 'e')
                && (std::tolower(static_cast<unsigned char>(str[pos + 1])) == 'n')
                && (std::tolower(static_cast<unsigned char>(str[pos + 2])) == 'v')
                && (str[pos + 3] == '(');
        };

        auto trim_inplace = [](std::string &t) {
            const char* ws = " \t\n\r";
            size_t a = t.find_first_not_of(ws);
            if (a == std::string::npos) { t.clear(); return; }
            size_t b = t.find_last_not_of(ws);
            t = t.substr(a, b - a + 1);
        };

        // Helper to parse a quoted string starting at pos. Returns parsed string and advances pos.
        auto parse_quoted = [&](size_t &pos) -> std::string {
            if (pos >= s.size()) throw std::runtime_error("unexpected end while parsing quoted string at position " + std::to_string(pos));
            char quote = s[pos];
            if (quote != '\'' && quote != '"') throw std::runtime_error("expected quote at position " + std::to_string(pos));
            ++pos; // consume opening quote
            std::string out;
            while (pos < s.size()) {
                char c = s[pos++];
                if (c == '\\') {
                    if (pos >= s.size()) throw std::runtime_error("invalid escape at end of input while parsing quoted string starting at position " + std::to_string(pos));
                    char esc = s[pos++];
                    // support common escapes
                    switch (esc) {
                        case 'n': out.push_back('\n'); break;
                        case 'r': out.push_back('\r'); break;
                        case 't': out.push_back('\t'); break;
                        case '\\': out.push_back('\\'); break;
                        case '\'': out.push_back('\''); break;
                        case '"': out.push_back('"'); break;
                        default: out.push_back(esc); break;
                    }
                } else if (c == quote) {
                    return out; // closed successfully
                } else {
                    out.push_back(c);
                }
            }
            throw std::runtime_error("unclosed quoted string starting at position " + std::to_string(pos));
        };

        // Helper to parse an unquoted token (identifier or bare default) until comma or ')'.
        auto parse_unquoted = [&](size_t &pos) -> std::string {
            size_t start = pos;
            while (pos < s.size() && s[pos] != ',' && s[pos] != ')') ++pos;
            std::string out = s.substr(start, pos - start);
            trim_inplace(out);
            return out;
        };

        std::string result;
        size_t last_pos = 0;
        size_t pos = 0;
        bool found_any = false;

        while (pos < s.size()) {
            // find next env(
            size_t i = pos;
            for (; i + 3 < s.size(); ++i) {
                if (ci_match_env(s, i)) break;
            }
            if (i + 3 >= s.size() || !ci_match_env(s, i)) break; // no more matches

            found_any = true;
            // append text before match
            result.append(s.substr(last_pos, i - last_pos));

            size_t p = i + 4; // position after "env(' or env("
            auto skip_ws = [&](void) {
                while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p]))) ++p;
            };

            skip_ws();
            if (p >= s.size()) throw std::runtime_error("unterminated env(...) starting at position " + std::to_string(i));

            // parse key
            std::string key;
            if (s[p] == '\'' || s[p] == '"') {
                key = parse_quoted(p);
                skip_ws();
            } else {
                key = parse_unquoted(p);
                if (key.empty()) throw std::runtime_error("empty env() key starting at position " + std::to_string(i));
            }

            skip_ws();

            // optional default
            std::string def;
            if (p < s.size() && s[p] == ',') {
                ++p; // consume comma
                skip_ws();
                if (p >= s.size()) throw std::runtime_error("missing default value in env() starting at position " + std::to_string(i));

                if (s[p] == '\'' || s[p] == '"') {
                    def = parse_quoted(p);
                    skip_ws();
                } else {
                    def = parse_unquoted(p);
                }
            }

            skip_ws();
            if (p >= s.size() || s[p] != ')') throw std::runtime_error("missing closing ')' for env() starting at position " + std::to_string(i));
            ++p; // consume ')'

            // lookup env value
            std::string val = Config::env(key, def);
            result.append(val);

            last_pos = p;
            pos = p;
        }

        if (!found_any) {
            return s; // no env() occurrences found
        }

        // append remainder
        result.append(s.substr(last_pos));
        return result;
    }

    static std::vector<std::string> split_key(const std::string& key) {
        std::vector<std::string> parts;
        size_t start = 0;
        size_t end = key.find('.');
        
        while (end != std::string::npos) {
            parts.push_back(key.substr(start, end - start));
            start = end + 1;
            end = key.find('.', start);
        }
        parts.push_back(key.substr(start));
        
        return parts;
    }

    static std::vector<std::string> split_array(const std::string& str) {
        std::vector<std::string> result;
        std::string item;
        std::istringstream stream(str);
        
        while (std::getline(stream, item, ',')) {
            // Trim whitespace
            item.erase(0, item.find_first_not_of(" \t\n\r"));
            item.erase(item.find_last_not_of(" \t\n\r") + 1);
            result.push_back(item);
        }
        
        return result;
    }
    
    std::unordered_map<std::string, std::string> values_;
};

} // namespace breeze::core

