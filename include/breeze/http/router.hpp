#pragma once

#include <breeze/http/middleware.hpp>
#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>

#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace breeze::http {

class Router {
public:
    using Handler = std::function<Response(const Request&)>;
    using Middleware = MiddlewarePipeline::Middleware;

    class RouteDefinition {
    public:
        RouteDefinition() = default;

        RouteDefinition& name(std::string name);
        RouteDefinition& middleware(Middleware mw);

    private:
        friend class Router;

        RouteDefinition(Router* router, std::string key) : router_(router), key_(std::move(key)) {}

        Router* router_ = nullptr;
        std::string key_;
    };

    RouteDefinition add(std::string method, std::string path, Handler handler);

    RouteDefinition get(std::string path, Handler handler) { return add("GET", std::move(path), std::move(handler)); }
    RouteDefinition post(std::string path, Handler handler) { return add("POST", std::move(path), std::move(handler)); }

    Response dispatch(const Request& request) const;

private:
    struct RouteEntry {
        Handler handler;
        std::string name;
        std::vector<Middleware> middlewares;
    };

    static std::string make_key(const std::string& method, const std::string& path);

    std::unordered_map<std::string, RouteEntry> routes_;
};

class Route {
public:
    using Handler = Router::Handler;
    using Middleware = Router::Middleware;

    class Group {
    public:
        Group prefix(std::string prefix) const;
        Group middleware(Middleware mw) const;

        template <typename Fn>
        void group(Fn&& fn) const
        {
            ScopedGroup scoped(*this);
            std::forward<Fn>(fn)();
        }

    private:
        friend class Route;

        Group(std::string prefix, std::vector<Middleware> middlewares)
            : prefix_(std::move(prefix)), middlewares_(std::move(middlewares))
        {
        }

        std::string prefix_;
        std::vector<Middleware> middlewares_;

        class ScopedGroup {
        public:
            explicit ScopedGroup(const Group& group);
            ~ScopedGroup();

            ScopedGroup(const ScopedGroup&) = delete;
            ScopedGroup& operator=(const ScopedGroup&) = delete;

        private:
            bool active_ = false;
        };
    };

    static void set_router(Router& router) { router_ = &router; }

    static Group prefix(std::string prefix) { return Group(std::move(prefix), {}); }
    static Group middleware(Middleware mw) { return Group({}, {std::move(mw)}); }

    static Router::RouteDefinition get(std::string path, Handler handler);
    static Router::RouteDefinition post(std::string path, Handler handler);

private:
    struct Context {
        std::string prefix;
        std::vector<Middleware> middlewares;
    };

    static Router& router();
    static std::string normalize_prefix(std::string prefix);
    static std::string normalize_path(std::string path);
    static std::string join_paths(const std::string& prefix, const std::string& path);
    static std::string apply_context_prefix(std::string path);
    static std::vector<Middleware> gather_context_middlewares();

    static Router::RouteDefinition add(std::string method, std::string path, Handler handler);

    inline static Router* router_ = nullptr;
    inline static thread_local std::vector<Context> contexts_;
};

} // namespace breeze::http
