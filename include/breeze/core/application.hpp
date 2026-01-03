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
#include <iostream>
#include <breeze/http/redis_session_repository.hpp>

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
        // Create and register the global singleton so Application::instance() and
        // the created shared_ptr refer to the same Application.
        singleton_ = std::make_shared<Application>();
        instance_initialized_ = true;
        return singleton_;
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
        // Ensure singleton instance is set
        if (!instance_initialized_) {
            instance_initialized_ = true;
        }
        
        boot();
        
        // Finalize routing
        finalize_routing();

        std::string host = "0.0.0.0";
        // Print environment-aware startup message
        if (is_production()) {
            std::cout << "Production server started on http://" << host << ":" << port << std::endl;
        } else {
            std::cout << "Development server started on http://" << host << ":" << port << std::endl;
        }

        // Require session.driver in config (default to file if missing) similar to Laravel
        if (!config_.has("session.driver")) {
            std::cerr << "Warning: session.driver not set in configuration; defaulting to 'file' storage.\n";
            config_.set("session.driver", "file");
        }
        // Build session repository based on config (driver: file|redis)
        std::shared_ptr<breeze::http::ISessionRepository> repo;
        std::string driver = config_.get("session.driver", "file");
        if (driver == "redis") {
            std::string rhost = config_.get("session.redis.host", "127.0.0.1");
            int rport = std::stoi(config_.get("session.redis.port", "6379"));
            std::string prefix = config_.get("session.redis.prefix", "breeze:session:");
            repo = std::make_shared<breeze::http::RedisSessionRepository>(rhost, rport, prefix);
        } else {
            std::string dir = config_.get("session.file.dir", "/tmp/breeze_sessions");
            repo = std::make_shared<breeze::http::FileSessionRepository>(dir);
        }

        // Upload settings from config
        std::string upload_dir = config_.get("files.upload_dir", "/tmp");
        size_t max_request_size = std::stoull(config_.get("files.max_request_size", "0"));
        size_t max_file_size = std::stoull(config_.get("files.max_file_size", "0"));
        // Allowed extensions/types as comma separated lists
        auto split_csv = [](const std::string& s){ std::vector<std::string> out; std::istringstream ss(s); std::string item; while (std::getline(ss, item, ',')) { if (!item.empty()) { out.push_back(item); } } return out; };
        std::vector<std::string> allowed_exts = split_csv(config_.get("files.allowed_extensions", ""));
        std::vector<std::string> allowed_types = split_csv(config_.get("files.allowed_types", ""));

        breeze::http::Server server([this](const breeze::http::Request& req) {
            // Use this Application instance to handle the request (not a separate singleton)
            return this->handle(req);
        }, repo, upload_dir, max_request_size, max_file_size, allowed_exts, allowed_types, nullptr);

        server.listen(host, port);
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

        // Ensure kernel can propagate middleware aliases/groups into the router
        kernel_.boot();
    }
    
    // Singleton instance access
    static Application& instance() {
        if (!singleton_) {
            // Lazily create the singleton if not set
            singleton_ = std::make_shared<Application>();
            instance_initialized_ = true;
        }
        return *singleton_;
    }

    static bool has_instance() {
        return instance_initialized_;
    }
    
    // Environment helpers
    bool is_production() const { return config_.get("app.env") == "production"; }
    bool is_local() const { return config_.get("app.env") == "local"; }

    void finalize_routing();

private:
    static inline bool instance_initialized_ = false;
    static inline std::shared_ptr<Application> singleton_ = nullptr;
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

        // Session config overrides from ENV
        if (!config_.has("session.driver") || !breeze::support::Env::get("SESSION_DRIVER").empty())
            config_.set("session.driver", breeze::support::Env::get("SESSION_DRIVER", config_.get("session.driver", "file")));

        if (!config_.has("session.file.dir") || !breeze::support::Env::get("SESSION_FILE_DIR").empty())
            config_.set("session.file.dir", breeze::support::Env::get("SESSION_FILE_DIR", config_.get("session.file.dir", "/tmp/breeze_sessions")));

        if (!config_.has("session.redis.host") || !breeze::support::Env::get("SESSION_REDIS_HOST").empty())
            config_.set("session.redis.host", breeze::support::Env::get("SESSION_REDIS_HOST", config_.get("session.redis.host", "127.0.0.1")));

        if (!config_.has("session.redis.port") || !breeze::support::Env::get("SESSION_REDIS_PORT").empty())
            config_.set("session.redis.port", breeze::support::Env::get("SESSION_REDIS_PORT", config_.get("session.redis.port", "6379")));

        if (!config_.has("session.redis.prefix") || !breeze::support::Env::get("SESSION_REDIS_PREFIX").empty())
            config_.set("session.redis.prefix", breeze::support::Env::get("SESSION_REDIS_PREFIX", config_.get("session.redis.prefix", "breeze:session:")));
    }
    
    Container container_;
    Config config_;
    Kernel kernel_;
    std::vector<std::shared_ptr<ServiceProvider>> service_providers_;
};

} // namespace breeze::core

