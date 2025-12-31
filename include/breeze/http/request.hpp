// include/breeze/http/request.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <utility>
#include <nlohmann/json.hpp>
#include <sstream>
#include <algorithm>
#include <memory>

namespace breeze::http {

// Simple per-request session store interface. Real apps can provide a shared
// session store and set it on the Request (set_session_store). This is
// intentionally minimal: it stores string->json values and supports flash data.
struct SessionStore {
    std::unordered_map<std::string, nlohmann::json> data;
    std::unordered_map<std::string, nlohmann::json> flash_next;
    std::unordered_map<std::string, nlohmann::json> flash_now;
};

class Request {
public:
    Request() = default;
    
    // Getters
    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& body() const { return body_; }
    const std::string& query_string() const { return query_string_; }

    // Allow wiring a session store (usually by server infra or Application) so
    // Request::session() and flash() behave across requests.
    void set_session_store(std::shared_ptr<SessionStore> store) { session_ = std::move(store); }

    // Basic session API (Laravel-like)
    nlohmann::json session(const std::string& key, nlohmann::json fallback = {}) const {
        if (!session_) return fallback;
        auto it = session_->data.find(key);
        if (it == session_->data.end()) return fallback;
        return it->second;
    }

    void session_put(const std::string& key, const nlohmann::json& value) {
        if (!session_) session_ = std::make_shared<SessionStore>();
        session_->data[key] = value;
    }

    bool session_has(const std::string& key) const {
        if (!session_) return false;
        return session_->data.find(key) != session_->data.end();
    }

    // Flash for next request
    void flash(const std::string& key, const nlohmann::json& value) {
        if (!session_) session_ = std::make_shared<SessionStore>();
        session_->flash_next[key] = value;
    }

    // Retrieve flashed data for this request (set by previous request)
    nlohmann::json old(const std::string& key, nlohmann::json fallback = {}) const {
        if (!session_) return fallback;
        auto it = session_->flash_now.find(key);
        if (it == session_->flash_now.end()) return fallback;
        return it->second;
    }

    // Percent-decode a URL-encoded string (decodes %XX). Leaves '+' unchanged.
    static std::string url_decode(const std::string& s) {
        std::string out; out.reserve(s.size());
        auto hex = [](char ch)->int {
            if (ch >= '0' && ch <= '9') return ch - '0';
            if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
            if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
            return -1;
        };
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (c == '%' && i + 2 < s.size()) {
                int hi = hex(s[i+1]);
                int lo = hex(s[i+2]);
                if (hi >= 0 && lo >= 0) {
                    char decoded = static_cast<char>((hi << 4) | lo);
                    out.push_back(decoded);
                    i += 2;
                    continue;
                }
            }
            // leave '+' as '+' to preserve literal plus signs
            out.push_back(c);
        }
        return out;
    }

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

    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }

    // Cookies (parsed from Cookie header)
    void parse_cookies() const {
        if (cookies_parsed_) return;
        auto cookie_header = header("cookie");
        if (cookie_header.empty()) { cookies_parsed_ = true; return; }
        std::istringstream stream(cookie_header);
        std::string pair;
        while (std::getline(stream, pair, ';')) {
            size_t pos = pair.find('=');
            if (pos != std::string::npos) {
                std::string key = pair.substr(0, pos);
                std::string value = pair.substr(pos + 1);
                // trim spaces
                auto ltrim = [](std::string &s) {
                    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); }));
                };
                auto rtrim = [](std::string &s) {
                    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end());
                };
                ltrim(key); rtrim(key); ltrim(value); rtrim(value);
                cookies_[key] = value;
            }
        }
        cookies_parsed_ = true;
    }

    std::string cookie(const std::string& name, std::string fallback = {}) const {
        const_cast<Request*>(this)->parse_cookies();
        auto it = cookies_.find(name);
        if (it == cookies_.end()) return fallback;
        return it->second;
    }

    const std::unordered_map<std::string, std::string>& cookies() const {
        const_cast<Request*>(this)->parse_cookies();
        return cookies_;
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
                // URL decode keys and values at the request layer
                query_params_[url_decode(key)] = url_decode(value);
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
    
    // Form (application/x-www-form-urlencoded) parsing
    void parse_form() const {
        if (form_parsed_) return;
        if (body_.empty()) { form_parsed_ = true; return; }
        auto ct = header("content-type");
        if (ct.find("application/x-www-form-urlencoded") == std::string::npos) { form_parsed_ = true; return; }
        std::istringstream stream(body_);
        std::string pair;
        while (std::getline(stream, pair, '&')) {
            size_t pos = pair.find('=');
            if (pos != std::string::npos) {
                std::string key = pair.substr(0, pos);
                std::string value = pair.substr(pos + 1);
                form_params_[url_decode(key)] = url_decode(value);
            }
        }
        form_parsed_ = true;
    }

    std::string form(const std::string& key, std::string fallback = {}) const {
        const_cast<Request*>(this)->parse_form();
        auto it = form_params_.find(key);
        if (it == form_params_.end()) return fallback;
        return it->second;
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
        // Support dot notation for JSON
        if (is_json()) {
            auto j = json();
            try {
                // Use json::pointer for dot notation (/a/b) if key contains '/'
                if (key.find('/') != std::string::npos) {
                    auto ptr = nlohmann::json::json_pointer(key);
                    if (j.contains(ptr)) return j[ptr].get<T>();
                } else if (j.contains(key)) {
                    return j[key].get<T>();
                }
            } catch (...) {
                // Fall through
            }
        }
        
        // Then try form data
        auto f = form(key);
        if (!f.empty()) {
            if constexpr (std::is_same_v<T, std::string>) {
                return f;
            } else if constexpr (std::is_same_v<T, int>) {
                return std::stoi(f);
            } else if constexpr (std::is_same_v<T, bool>) {
                std::string lower = f;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                return lower == "true" || lower == "1" || lower == "yes";
            }
        }

        // Then query params
        auto q = query(key);
        if (!q.empty()) {
            if constexpr (std::is_same_v<T, std::string>) {
                return q;
            } else if constexpr (std::is_same_v<T, int>) {
                return std::stoi(q);
            } else if constexpr (std::is_same_v<T, bool>) {
                std::string lower = q;
                std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
                return lower == "true" || lower == "1" || lower == "yes";
            }
        }
        
        return fallback;
    }
    
    // Laravel-like helpers
    nlohmann::json all() const {
        nlohmann::json out = nlohmann::json::object();
        // Merge JSON body
        if (is_json()) {
            auto j = json();
            if (j.is_object()) {
                for (auto it = j.begin(); it != j.end(); ++it) out[it.key()] = it.value();
            }
        }
        // Merge form params
        const_cast<Request*>(this)->parse_form();
        for (auto &p : form_params_) out[p.first] = p.second;
        // Merge query params
        const_cast<Request*>(this)->parse_query_string();
        for (auto &p : query_params_) out[p.first] = p.second;
        return out;
    }

    nlohmann::json only(const std::vector<std::string>& keys) const {
        auto a = all();
        nlohmann::json out = nlohmann::json::object();
        for (auto &k : keys) if (a.contains(k)) out[k] = a[k];
        return out;
    }

    nlohmann::json except(const std::vector<std::string>& keys) const {
        auto a = all();
        for (auto &k : keys) if (a.contains(k)) a.erase(k);
        return a;
    }

    bool has(const std::string& key) const {
        auto a = all();
        return a.contains(key);
    }

    bool filled(const std::string& key) const {
        if (!has(key)) return false;
        auto val = all()[key];
        if (val.is_string()) return !val.get<std::string>().empty();
        if (val.is_array() || val.is_object()) return !val.empty();
        return true;
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
        form_parsed_ = false;
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

    mutable std::unordered_map<std::string, std::string> form_params_;
    mutable bool form_parsed_ = false;

    std::unordered_map<std::string, std::string> headers_;
    mutable std::unordered_map<std::string, std::string> cookies_;
    mutable bool cookies_parsed_ = false;
    std::unordered_map<std::string, std::string> params_; // Path parameters

    // Optional per-request session pointer
    std::shared_ptr<SessionStore> session_ = nullptr;
};

} // namespace breeze::http

