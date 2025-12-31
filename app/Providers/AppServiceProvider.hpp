// app/Providers/AppServiceProvider.hpp
#pragma once

#include <breeze/core/application.hpp>
#include "ViewServiceProvider.hpp"
#include "MiddlewareServiceProvider.hpp"
#include "ControllerServiceProvider.hpp"
#include "RouteServiceProvider.hpp"

namespace app::Providers {

class AppServiceProvider : public breeze::core::ServiceProvider {
public:
    using breeze::core::ServiceProvider::ServiceProvider;

    void register_services() override {
        // Register application-level service providers here. Order matters: register services first
        // so their register_services() are executed now; their boot() will be called later
        app_.register_provider<::app::Providers::ViewServiceProvider>();
        app_.register_provider<::app::Providers::MiddlewareServiceProvider>();
        app_.register_provider<::app::Providers::ControllerServiceProvider>();
        app_.register_provider<::app::Providers::RouteServiceProvider>();
    }

    void boot() override {
        // no-op: individual providers will be booted by Application::boot()
    }
};

} // namespace app::Providers

