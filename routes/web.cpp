#include <breeze/breeze.hpp>
#include "../app/Http/Controllers/UserController.hpp"
#include "../app/Http/Controllers/HomeController.hpp"

void register_web_routes(breeze::core::Application& app) {
    auto& router = app.kernel().router();

    // Simple welcome route
    router.get("/", [](const breeze::http::Request&) {
        return breeze::http::Response::ok("Welcome to Breeze Web!");
    }).name("web.home");

    // HomeController routes using Laravel-style controller method binding
    router.group({.prefix = "/home"}, [&](auto& group) {
        // Mimic Laravel's Route::controller(HomeController::class) style by binding methods directly
        group.get("/", &HomeController::index);
        group.get("/about", &HomeController::about);
        group.get("/contact", &HomeController::contact);
    });

    // UserController routes using controller methods, similar to Laravel's Route::controller(UserController::class)
    router.group({.prefix = "/users"}, [&](auto& group) {
        group.get("/", &UserController::index);
        group.get("/{id}", &UserController::show);
    });
}