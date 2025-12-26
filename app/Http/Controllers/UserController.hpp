#pragma once
#include <breeze/http/controller.hpp>
#include <breeze/http/response.hpp>
#include <breeze/http/request.hpp>

class UserController : public breeze::http::Controller {
public:
    breeze::http::Response index(const breeze::http::Request& req) {
        return breeze::http::Response::ok("User Index");
    }

    breeze::http::Response show(const breeze::http::Request& req) {
        std::string id = req.param("id");
        std::string name = req.query("name");
        return breeze::http::Response::ok("User Profile: " + id + " " + name);
    }
};
