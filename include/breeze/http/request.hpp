// include/breeze/http/request.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>

namespace breeze::http {

class Request {
public:
    Request() = default;
    
    // Getters
    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& body() const { return body_; }
    const std::string& query_string() const { return query_string_; }
    
    // Headers
    void set_header(std::string name, std::string value) { 
        std::transform(name.begin(), name.end(), name.begin(), ::tolower);
        headers_[std::move(name)] = std::move(value); 
    }
    
    std::string header(const std::string& name, std::string fallback = {}) const {
        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
        
        auto it = headers_.find(lower_name);
        if (it == headers_.end()) {
            return fallback;
        }
        return it->second;
    }
    
    // Query parameters
    void parse_query_string() {
        if (query_parsed_) return;
        
        std::istringstream stream(query_string_);
        std::string pair;
        
        while (std::getline(stream, pair, '&')) {
            size_t pos = pair.find('=');
            if (pos != std::string::npos) {
                std::string key = pair.substr(0, pos);
                std::string value = pair.substr(pos + 1);
                // URL decode would go here
                query_params_[key] = value;
            }
        }
        
        query_parsed_ = true;
    }
    
    std::string query(const std::string& key, std::string fallback = {}) const {
        const_cast<Request*>(this)->parse_query_string();
        
        auto it = query_params_.find(key);
        if (it == query_params_.end()) {
            return fallback;
        }
        return it->second;
    }
    
    template<typename T>
    T query(const std::string& key, T fallback = T()) const {
        auto str = query(key);
        if (str.empty()) return fallback;
        
        if constexpr (std::is_same_v<T, int>) {
            return std::stoi(str);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::stod(str);
        } else if constexpr (std::is_same_v<T, bool>) {
            std::string lower = str;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            return lower == "true" || lower == "1" || lower == "yes";
        } else {
            return static_cast<T>(str);
        }
    }
    
    // JSON body
    nlohmann::json json() const {
        if (!json_parsed_) {
            const_cast<Request*>(this)->parse_json();
        }
        return json_data_;
    }
    
    template<typename T>
    T input(const std::string& key, T fallback = T()) const {
        // First try JSON
        if (is_json()) {
            auto j = json();
            if (j.contains(key)) {
                try {
                    return j[key].get<T>();
                } catch (...) {
                    // Fall through
                }
            }
        }
        
        // Then try form data or query params
        auto str = query(key);
        if (!str.empty()) {
            if constexpr (std::is_same_v<T, std::string>) {
                return str;
            } else if constexpr (std::is_same_v<T, int>) {
                return std::stoi(str);
            } else if constexpr (std::is_same_v<T, bool>) {
                std::string lower = str;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                return lower == "true" || lower == "1" || lower == "yes";
            }
        }
        
        return fallback;
    }
    
    // Convenience methods
    bool is_json() const {
        return header("content-type").find("application/json") != std::string::npos;
    }
    
    bool expects_json() const {
        return header("accept").find("application/json") != std::string::npos ||
               header("content-type").find("application/json") != std::string::npos;
    }
    
    bool is(const std::string& pattern) const {
        // Simple pattern matching (could be enhanced)
        if (pattern == "*") return true;
        if (pattern.back() == '*') {
            std::string prefix = pattern.substr(0, pattern.length() - 1);
            return path_.find(prefix) == 0;
        }
        return path_ == pattern;
    }
    
    std::string bearer_token() const {
        auto auth = header("authorization");
        if (auth.find("Bearer ") == 0) {
            return auth.substr(7);
        }
        return {};
    }
    
    // Path parameters (set by router)
    void set_param(const std::string& key, const std::string& value) {
        params_[key] = value;
    }
    
    std::string param(const std::string& key, std::string fallback = {}) const {
        auto it = params_.find(key);
        if (it == params_.end()) {
            return fallback;
        }
        return it->second;
    }
    
    template<typename T>
    T param(const std::string& key, T fallback = T()) const {
        auto str = param(key);
        if (str.empty()) return fallback;
        
        if constexpr (std::is_same_v<T, int>) {
            return std::stoi(str);
        } else if constexpr (std::is_same_v<T, std::string>) {
            return str;
        } else {
            return static_cast<T>(str);
        }
    }
    
    // Setters
    void set_method(std::string method) { 
        std::transform(method.begin(), method.end(), method.begin(), ::toupper);
        method_ = std::move(method); 
    }
    
    void set_path(std::string path) { 
        // Extract query string if present
        size_t query_pos = path.find('?');
        if (query_pos != std::string::npos) {
            query_string_ = path.substr(query_pos + 1);
            path_ = path.substr(0, query_pos);
        } else {
            path_ = std::move(path);
        }
    }
    
    void set_body(std::string body) { 
        body_ = std::move(body); 
        json_parsed_ = false;
    }
    
    void set_query_string(std::string query) { 
        query_string_ = std::move(query); 
        query_parsed_ = false;
    }

private:
    void parse_json() {
        if (body_.empty()) {
            json_data_ = nlohmann::json::object();
        } else {
            try {
                json_data_ = nlohmann::json::parse(body_);
            } catch (...) {
                json_data_ = nlohmann::json::object();
            }
        }
        json_parsed_ = true;
    }
    
    std::string method_ = "GET";
    std::string path_ = "/";
    std::string body_;
    std::string query_string_;
    
    mutable std::unordered_map<std::string, std::string> query_params_;
    mutable bool query_parsed_ = false;
    
    mutable nlohmann::json json_data_;
    mutable bool json_parsed_ = false;
    
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> params_; // Path parameters
};

} // namespace breeze::http