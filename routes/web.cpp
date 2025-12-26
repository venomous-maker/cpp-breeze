#include <breeze/breeze.hpp>
#include "../app/Http/Controllers/UserController.hpp"
#include "../app/Http/Controllers/HomeController.hpp"

void register_web_routes(breeze::core::Application& app) {
    auto& router = app.kernel().router();

    router.get("/", [](const breeze::http::Request&) {
        return breeze::http::Response::ok("Welcome to Breeze Web!");
    }).name("web.home");

    // Home Controller routes
    router.controller<HomeController>("/home").group([](auto& group) {
        group.get("/", &HomeController::index);
        group.get("/about", &HomeController::about);
        group.get("/contact", &HomeController::contact);
    });

    // User Controller routes
    router.controller<UserController>("/users").group([](auto& group) {
        group.get("/", &UserController::index);
        group.get("/{id}", &UserController::show);
    });
}
