// app/Providers/MiddlewareServiceProvider.hpp
#pragma once

#include <breeze/core/application.hpp>
#include <app/Http/Middleware/RequestLogger.hpp>

namespace app::Providers {

class MiddlewareServiceProvider : public breeze::core::ServiceProvider {
public:
    using breeze::core::ServiceProvider::ServiceProvider;

    void register_services() override {
        // Register named middleware alias so routes can refer to it by name
        app_.kernel().register_middleware_alias("request.logger", app::Http::Middleware::RequestLogger());

        // Define a common 'web' group that includes the request logger
        app_.kernel().register_middleware_group("web", {"request.logger"});
    }

    void boot() override {
        // no-op for now
    }
};

} // namespace app::Providers

