// include/breeze/core/application.hpp
#pragma once
#include <breeze/core/config.hpp>
#include <breeze/core/container.hpp>
#include <breeze/core/kernel.hpp>
#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>
#include <breeze/http/server.hpp>
#include <breeze/support/env.hpp>
#include <memory>
#include <functional>
#include <vector>

namespace breeze::core {

class Application;

// Base service provider interface
class ServiceProvider {
public:
    explicit ServiceProvider(Application& app) : app_(app) {}
    virtual ~ServiceProvider() = default;
    
    virtual void register_services() = 0;
    virtual void boot() {}
    
protected:
    Application& app_;
};

class Application {
public:
    Application() {
        bootstrap();
    }

    static std::shared_ptr<Application> create() {
        return std::make_shared<Application>();
    }
    
    Container& container() { return container_; }
    const Container& container() const { return container_; }

    Config& config() { return config_; }
    const Config& config() const { return config_; }

    Kernel& kernel() { return kernel_; }
    const Kernel& kernel() const { return kernel_; }

    breeze::http::Response handle(const breeze::http::Request& request) const { 
        return kernel_.handle(request); 
    }

    void run(int port = 8080) {
        boot();
        
        // Finalize routing
        finalize_routing();

        breeze::http::Server server([this](const breeze::http::Request& req) {
            return this->handle(req);
        });
        server.listen("0.0.0.0", port);
    }
    
    // Laravel-style service provider registration
    template<typename Provider>
    void register_provider() {
        auto provider = std::make_shared<Provider>(*this);
        provider->register_services();
        service_providers_.push_back(provider);
    }
    
    void boot() {
        for (auto& provider : service_providers_) {
            provider->boot();
        }
    }
    
    // Singleton instance access
    static Application& instance() {
        static auto app = Application::create();
        return *app;
    }
    
    // Environment helpers
    bool is_production() const { return config_.get("app.env") == "production"; }
    bool is_local() const { return config_.get("app.env") == "local"; }

    void finalize_routing();

private:
    void bootstrap() {
        // Load .env file
        breeze::support::Env::load(".env");

        // Register core services
        // We can't easily pass 'this' as shared_ptr here if it's not managed by shared_ptr yet during construction
        // But Application::create() handles it.
        
        // Load configuration
        load_configuration();
    }
    
    void load_configuration() {
        // Load from config/ directory
        config_.load_from_path("config");

        // Override with .env if set, or set defaults if not in config
        if (!config_.has("app.name") || !breeze::support::Env::get("APP_NAME").empty())
            config_.set("app.name", breeze::support::Env::get("APP_NAME", config_.get("app.name", "Breeze Application")));
        
        if (!config_.has("app.env") || !breeze::support::Env::get("APP_ENV").empty())
            config_.set("app.env", breeze::support::Env::get("APP_ENV", config_.get("app.env", "local")));
            
        if (!config_.has("app.debug") || !breeze::support::Env::get("APP_DEBUG").empty())
            config_.set("app.debug", breeze::support::Env::get("APP_DEBUG", config_.get("app.debug", "true")));
            
        if (!config_.has("app.url") || !breeze::support::Env::get("APP_URL").empty())
            config_.set("app.url", breeze::support::Env::get("APP_URL", config_.get("app.url", "http://localhost:8080")));
    }
    
    Container container_;
    Config config_;
    Kernel kernel_;
    std::vector<std::shared_ptr<ServiceProvider>> service_providers_;
};

} // namespace breeze::core