#pragma once

#include <string>
#include <unordered_map>
#include <utility>

namespace breeze::http {

class Request {
public:
    const std::string& method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& body() const { return body_; }

    void set_method(std::string method) { method_ = std::move(method); }
    void set_path(std::string path) { path_ = std::move(path); }
    void set_body(std::string body) { body_ = std::move(body); }

    void set_header(std::string name, std::string value) { headers_[std::move(name)] = std::move(value); }

    std::string header(const std::string& name, std::string fallback = {}) const
    {
        auto it = headers_.find(name);
        if (it == headers_.end()) {
            return fallback;
        }
        return it->second;
    }

private:
    std::string method_ = "GET";
    std::string path_ = "/";
    std::string body_;
    std::unordered_map<std::string, std::string> headers_;
};

} // namespace breeze::http
