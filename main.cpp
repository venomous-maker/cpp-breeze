#include <breeze/breeze.hpp>
#include "app/Providers/ViewServiceProvider.hpp"
#include "app/Providers/MiddlewareServiceProvider.hpp"
#include "app/Providers/ControllerServiceProvider.hpp"

// Forward declarations of route registration functions
void register_web_routes(breeze::core::Application& app);
void register_api_routes(breeze::core::Application& app);
void register_admin_routes(breeze::core::Application& app);

int main(int argc, char** argv) {
    auto app = breeze::core::Application::create();

    // Register service providers
    app->register_provider<::app::Providers::ViewServiceProvider>();
    app->register_provider<::app::Providers::MiddlewareServiceProvider>();
    app->register_provider<::app::Providers::ControllerServiceProvider>();

    // Register routes from the routes directory
    register_web_routes(*app);
    register_api_routes(*app);
    register_admin_routes(*app);

    // Default port from env or 8000
    int port = std::stoi(breeze::support::Env::get("APP_PORT", "8000"));
    
    if (argc > 1) {
        try {
            port = std::stoi(argv[1]);
        } catch (...) {}
    }

    app->run(port);

    return 0;
}
