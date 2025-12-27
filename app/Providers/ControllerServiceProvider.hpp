#pragma once

#include <breeze/core/application.hpp>
#include <app/Http/Controllers/HomeController.hpp>
#include <app/Http/Controllers/UserController.hpp>

namespace app::Providers {

class ControllerServiceProvider : public breeze::core::ServiceProvider {
public:
    using breeze::core::ServiceProvider::ServiceProvider;

    void register_services() override {
        // Register controller factories and actions with the router so string-style handlers resolve
        auto& router = app_.kernel().router();
        router.register_controller<HomeController>("HomeController");
        router.register_controller_action<HomeController>("HomeController", "index", &HomeController::index);
        router.register_controller_action<HomeController>("HomeController", "about", &HomeController::about);
        router.register_controller_action<HomeController>("HomeController", "contact", &HomeController::contact);
        router.register_controller_action<HomeController>("HomeController", "inlineBreeze", &HomeController::inlineBreeze);
        router.register_controller_action<HomeController>("HomeController", "inlineCpp", &HomeController::inlineCpp);

        router.register_controller<UserController>("UserController");
        router.register_controller_action<UserController>("UserController", "index", &UserController::index);
        router.register_controller_action<UserController>("UserController", "show", &UserController::show);
    }

    void boot() override {
        // no-op
    }
};

} // namespace app::Providers

