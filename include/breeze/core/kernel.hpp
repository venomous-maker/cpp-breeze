#pragma once

#include <breeze/http/middleware.hpp>
#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>
#include <breeze/http/router.hpp>

#include <memory>
#include <vector>
#include <unordered_map>
#include <string>

namespace breeze::core {

class Kernel {
public:
    Kernel() = default;

    breeze::http::Router& router() { return router_; }
    const breeze::http::Router& router() const { return router_; }

    breeze::http::MiddlewarePipeline& middleware() { return middleware_; }
    const breeze::http::MiddlewarePipeline& middleware() const { return middleware_; }

    // Handle request (implementation may live in src/core/kernel.cpp)
    breeze::http::Response handle(const breeze::http::Request& request) const;

    void set_container(breeze::core::Container* container) {
        router_.set_container(container);
    }

    // Boot sequence: propagate middleware aliases/groups into router
    void boot() {
        for (const auto& [name, mw] : middleware_aliases_) {
            router_.aliasMiddleware(name, mw);
        }
        for (const auto& [group, aliases] : middleware_groups_) {
            router_.registerMiddlewareGroup(group, aliases);
        }

        // router's container should be set by the application via set_container
    }

    // Middleware alias / group registration
    void register_middleware_alias(const std::string& name, breeze::http::Router::Middleware mw) {
        middleware_aliases_[name] = std::move(mw);
    }

    void register_middleware_group(const std::string& group, const std::vector<std::string>& aliases) {
        middleware_groups_[group] = aliases;
    }

private:
    breeze::http::Router router_;
    breeze::http::MiddlewarePipeline middleware_;


    std::unordered_map<std::string, breeze::http::Router::Middleware> middleware_aliases_;
    std::unordered_map<std::string, std::vector<std::string>> middleware_groups_;
};

} // namespace breeze::core
