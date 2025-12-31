// app/Providers/RouteServiceProvider.hpp
#pragma once

#include <breeze/core/application.hpp>

// Prefer the CMake-generated aggregator header. If it's not available (e.g., before configure),
// fall back to including individual route headers and provide a small local `register_routes`
// wrapper that forwards to the per-file register_*_routes functions. This keeps development
// and static analysis smooth while still using the generated aggregator in normal builds.

#if __has_include(<routes_all.hpp>)
#include <routes_all.hpp>
#else
#include "../../routes/web.hpp"
#include "../../routes/api.hpp"
#include "../../routes/admin.hpp"

inline void register_routes(breeze::core::Application& app) {
    register_web_routes(app);
    register_api_routes(app);
    register_admin_routes(app);
}
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
