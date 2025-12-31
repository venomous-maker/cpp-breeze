// include/breeze/http/response.hpp
#pragma once
#include <breeze/http/status_code.hpp>
#include <string>
#include <sstream>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <utility>
#include <memory>

namespace breeze::http {

class Response {
public:
    Response() = default;
    
    explicit Response(StatusCode status, std::string body = "") 
        : status_(status), body_(std::move(body)) {}
    
    // Status
    StatusCode status() const { return status_; }
    void set_status(StatusCode status) { status_ = status; }
    void set_status(int status) { status_ = static_cast<StatusCode>(status); }

    // Fluent status
    Response& with_status(StatusCode status) { set_status(status); return *this; }
    Response& with_status(int status) { set_status(status); return *this; }

    // Body
    const std::string& body() const { return body_; }
    void set_body(std::string body) { body_ = std::move(body); }
    Response& with_body(std::string body) { set_body(std::move(body)); return *this; }

    // Headers
    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }
    
    void set_header(std::string name, std::string value) { 
        headers_[std::move(name)] = std::move(value); 
    }
    
    Response& with_header(std::string name, std::string value) { set_header(std::move(name), std::move(value)); return *this; }

    std::string header(const std::string& name, const std::string& fallback = {}) const {
        auto it = headers_.find(name);
        return it != headers_.end() ? it->second : fallback;
    }
    
    // Convenience header setters
    void content_type(const std::string& type) {
        set_header("Content-Type", type);
    }
    
    Response& with_content_type(const std::string& type) { content_type(type); return *this; }

    void location(const std::string& url) {
        set_header("Location", url);
    }
    
    Response& with_location(const std::string& url) { location(url); return *this; }

    // Cookie support (basic)
    void cookie(const std::string& name, const std::string& value,
                int max_age = 3600, const std::string& path = "/",
                bool http_only = true, bool secure = false) {
        std::ostringstream cookie;
        cookie << name << "=" << value
               << "; Max-Age=" << max_age
               << "; Path=" << path;
        
        if (http_only) cookie << "; HttpOnly";
        if (secure) cookie << "; Secure";
        
        // Append to existing Set-Cookie headers
        auto it = headers_.find("Set-Cookie");
        if (it != headers_.end()) {
            it->second += "\r\nSet-Cookie: " + cookie.str();
        } else {
            set_header("Set-Cookie", cookie.str());
        }
    }

    Response& with_cookie(const std::string& name, const std::string& value,
                int max_age = 3600, const std::string& path = "/",
                bool http_only = true, bool secure = false) {
        cookie(name, value, max_age, path, http_only, secure);
        return *this;
    }

    // Response building helpers
    static Response ok(std::string body = "") {
        return Response{StatusCode::OK, std::move(body)};
    }
    
    static Response json(const nlohmann::json& data, int status = 200) {
        auto res = Response{static_cast<StatusCode>(status), data.dump()};
        res.content_type("application/json");
        return res;
    }
    
    static Response json(const std::string& json_str, int status = 200) {
        auto res = Response{static_cast<StatusCode>(status), json_str};
        res.content_type("application/json");
        return res;
    }
    
    template<typename T>
    static Response json(const T& data, int status = 200) {
        nlohmann::json j = data;
        return json(j, status);
    }
    
    static Response redirect(const std::string& url, int status = 302) {
        Response res{static_cast<StatusCode>(status)};
        res.location(url);
        return res;
    }
    
    // View rendering support: the application should provide a view renderer function
    // that maps (template_name, data) -> rendered string. We allow a global function
    // pointer to be set by the application bootstrap (view_renderer = ...).
    using ViewRendererFn = std::function<std::string(const std::string&, const nlohmann::json&)>;
    static void set_view_renderer(ViewRendererFn fn) { view_renderer_ = std::move(fn); }


    static Response not_found(std::string message = "Not Found") {
        return Response{StatusCode::NotFound, std::move(message)};
    }
    
    static Response unauthorized(std::string message = "Unauthorized") {
        return Response{StatusCode::Unauthorized, std::move(message)};
    }
    
    static Response forbidden(std::string message = "Forbidden") {
        return Response{StatusCode::Forbidden, std::move(message)};
    }
    
    static Response error(std::string message = "Internal Server Error") {
        return Response{StatusCode::InternalServerError, std::move(message)};
    }
    
    static Response bad_request(std::string message = "Bad Request") {
        return Response{StatusCode::BadRequest, std::move(message)};
    }
    
    // Streaming response (for large files or Server-Sent Events)
    class Stream {
    public:
        virtual void write(const std::string& data) = 0;
        virtual void end() = 0;
        virtual ~Stream() = default;
    };
    
    inline std::string to_string() const;

private:
    StatusCode status_ = StatusCode::OK;
    std::string body_;
    std::unordered_map<std::string, std::string> headers_;
    static inline ViewRendererFn view_renderer_ = nullptr;
};

inline std::string Response::to_string() const {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << static_cast<int>(status_);
    
    // Add status text if available
    std::string reason = Status::reason_phrase(status_);
    if (!reason.empty()) {
        oss << " " << reason;
    }
    oss << "\r\n";
    
    // Default headers if not set
    auto final_headers = headers_;
    if (!final_headers.contains("Content-Type")) {
        final_headers["Content-Type"] = "text/plain";
    }
    if (!final_headers.contains("Content-Length")) {
        final_headers["Content-Length"] = std::to_string(body_.size());
    }
    
    for (const auto& [k, v] : final_headers) {
        oss << k << ": " << v << "\r\n";
    }
    oss << "\r\n" << body_;
    
    return oss.str();
}

} // namespace breeze::http

