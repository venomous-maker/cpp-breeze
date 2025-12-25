#pragma once

#include <breeze/http/status_code.hpp>
#include <string>
#include <sstream>
#include <unordered_map>

namespace breeze::http {

class Response {
public:
    Response() = default;

    explicit Response(StatusCode status, std::string body = "") : status_(status), body_(std::move(body)) {}
    
    StatusCode status() const { return status_; }
    const std::string& body() const { return body_; }
    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }

    void set_status(StatusCode status) { status_ = status; }
    void set_status(int status) { status_ = static_cast<StatusCode>(status); }
    void set_body(std::string body) { body_ = std::move(body); }

    void set_header(std::string name, std::string value) { headers_[std::move(name)] = std::move(value); }
    
    std::string to_string() const;

    std::string header(const std::string& name, const std::string& fallback = {}) const {
        auto it = headers_.find(name);
        return it != headers_.end() ? it->second : fallback;
    }

    template <typename T>
    void set_header(std::string name, T value) { headers_[std::move(name)] = std::to_string(value); }
    
    // Helper methods for common responses
    static Response ok(std::string body = "") {
        return Response{StatusCode::OK, std::move(body)};
    }
    
    static Response json(const std::string& json) {
        auto res = Response{StatusCode::OK, json};
        res.set_header("Content-Type", "application/json");
        return res;
    }

private:
    StatusCode status_ = StatusCode::OK;
    std::string body_;
    std::unordered_map<std::string, std::string> headers_;
};

} // namespace breeze::http
