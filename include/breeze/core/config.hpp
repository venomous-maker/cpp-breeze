// include/breeze/core/config.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

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
            return std::string(val);
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
            values_[current_key] = json.dump();
        }
    }
    
    std::vector<std::string> split_key(const std::string& key) const {
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
    
    std::vector<std::string> split_array(const std::string& str) const {
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