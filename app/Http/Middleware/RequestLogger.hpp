#pragma once

#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>
#include <breeze/http/router.hpp>
#include <iostream>

namespace app::Http::Middleware {

// Laravel-like request logger middleware
inline breeze::http::Router::Middleware RequestLogger() {
    return [](const breeze::http::Request& req, const breeze::http::Router::Handler& next) -> breeze::http::Response {
        // Call the next middleware / handler first so we can log status
        try {
            auto res = next(req);
            std::string method = req.method();
            std::string path = req.path();
            std::string ip = req.header("x-remote-addr", "unknown");
            int status = static_cast<int>(res.status());
            std::cout << "[Request] " << method << " " << path << " - " << ip << " - " << status << std::endl;
            return res;
        } catch (const std::exception& e) {
            std::cout << "[Request] " << req.method() << " " << req.path() << " - " << req.header("x-remote-addr", "unknown") << " - " << "500 (exception: " << e.what() << ")" << std::endl;
            return breeze::http::Response::error(std::string("Controller action failed: ") + e.what());
        }
    };
}

} // namespace app::Http::Middleware

