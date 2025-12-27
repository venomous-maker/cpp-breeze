#pragma once
#include <breeze/http/controller.hpp>
#include <breeze/http/response.hpp>
#include <breeze/http/request.hpp>

class HomeController : public breeze::http::Controller {
public:
    breeze::http::Response index(const breeze::http::Request& req) {
        return breeze::http::Response::view("index", {
            {"title", "Breeze Home"},
            {"message", "Hello from Breeze View Engine!"},
            {"show_user", true},
            {"user", {{"name", "Breeze"}}},
            {"features", {"Fast", "Laravel-inspired", "C++20", "Service Container"}}
        });
    }

    breeze::http::Response about(const breeze::http::Request& req) {
        return breeze::http::Response::ok("Breeze is a Laravel-inspired C++ web framework.");
    }
    
    breeze::http::Response contact(const breeze::http::Request& req) {
        return breeze::http::Response::ok("Contact us at support@breeze-framework.com");
    }

    breeze::http::Response inlineBreeze(const breeze::http::Request& req) {
        return breeze::http::Response::view("inline_breeze_example", {
            {"greeting", "Hello Breeze"},
            {"count", 3},
            {"items", {"one", "two", "three"}}
        });
    }

    breeze::http::Response inlineCpp(const breeze::http::Request& req) {
        return breeze::http::Response::view("inline_cpp_example", {});
    }
};
