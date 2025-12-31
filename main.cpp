#include <breeze/breeze.hpp>
#include "app/Providers/AppServiceProvider.hpp"

int main(int argc, char** argv) {
    auto app = breeze::core::Application::create();

    // Register the top-level AppServiceProvider which in turn registers other providers
    app->register_provider<::app::Providers::AppServiceProvider>();

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
