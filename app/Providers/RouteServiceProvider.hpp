// app/Providers/RouteServiceProvider.hpp
#pragma once

#include <breeze/core/application.hpp>

// Require the CMake-generated aggregator header. This repository now enforces that
// the configure step generates <routes_all.hpp> which aggregates all headers in
// routes/ and provides a single inline register_routes(Application&).
#if __has_include(<routes_all.hpp>)
#include <routes_all.hpp>
#else
#error "Missing generated routes_all.hpp. Run CMake configure to generate routes_all.hpp from the routes/ headers."
#endif

namespace app::Providers {

class RouteServiceProvider : public breeze::core::ServiceProvider {
public:
    using breeze::core::ServiceProvider::ServiceProvider;

    void register_services() override {
        // No container bindings required for routes
    }

    void boot() override {
        // Register routes during boot so other providers (controllers, middleware) have been registered
        register_routes(app_);
    }
};

} // namespace app::Providers
