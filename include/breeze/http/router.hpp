// include/breeze/http/router.hpp
#pragma once
#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>
#include <breeze/http/middleware.hpp>
#include <breeze/http/controller.hpp>
#include <breeze/core/container.hpp>
#include <functional>
#include <memory>
#include <ranges>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <type_traits>
#include <optional>
#include <sstream> // added for istringstream

namespace breeze::http {

class Router {
public:
    using Handler = std::function<Response(const Request&)>;
    using Middleware = std::function<Response(const Request&, const Handler&)>;
    
    struct Route {
        std::string method;
        std::string pattern_str;
        std::regex pattern;
        std::vector<std::string> param_names;
        Handler handler;
        std::string route_name;
        std::vector<Middleware> middlewares;
        std::vector<std::string> middleware_aliases;

        [[nodiscard]] bool matches(const std::string& req_method, const std::string& path) const {
            if (this->method != req_method) return false;

            // Normalize path for comparison
            std::string normalized_path = path;

            // Remove trailing slash for comparison (but allow root)
            if (normalized_path.size() > 1 && normalized_path.back() == '/') {
                normalized_path.pop_back();
            }

            // Ensure path starts with /
            if (normalized_path.empty() || normalized_path.front() != '/') {
                normalized_path = "/" + normalized_path;
            }

            // For root path, ensure we have exactly "/"
            if (normalized_path.empty() || normalized_path == "/") {
                return std::regex_match("/", pattern);
            }

            return std::regex_match(normalized_path, pattern);
        }
        
        [[nodiscard]] std::unordered_map<std::string, std::string> extract_params(const std::string& path) const {
            std::unordered_map<std::string, std::string> params;
            std::smatch matches;
            
            std::string normalized_path = path;
            // Normalize leading and trailing slashes similar to matches()
            if (normalized_path.size() > 1 && normalized_path.back() == '/') {
                normalized_path.pop_back();
            }
            if (normalized_path.empty() || normalized_path.front() != '/') {
                normalized_path = "/" + normalized_path;
            }

            if (std::regex_match(normalized_path, matches, pattern)) {
                for (size_t i = 0; i < param_names.size() && i + 1 < (size_t)matches.size(); ++i) {
                    params[param_names[i]] = matches[i + 1].str();
                }
            }
            
            return params;
        }

        Route& name(std::string name) {
            this->route_name = std::move(name);
            return *this;
        }

        Route& middleware(Middleware mw) {
            this->middlewares.push_back(std::move(mw));
            return *this;
        }

        Route& middleware(std::string middleware_alias) {
            this->middleware_aliases.push_back(std::move(middleware_alias));
            return *this;
        }

        // Add convenience for multiple aliases
        Route& middleware(const std::vector<std::string>& aliases) {
            for (const auto& a : aliases) middleware(a);
            return *this;
        }
    };

    // Basic routing
    Route& add_route(const std::string& method, const std::string& pattern, Handler handler) {
        auto [param_names, regex_pattern] = compile_pattern(pattern);
        
        routes_.push_back({
            method,
            pattern,
            std::regex(regex_pattern),
            param_names,
            std::move(handler),
            "", // name
            {},  // middleware
            {}   // middleware_aliases
        });
        
        return routes_.back();
    }
    
    // Laravel-like match/any helpers
    Route& match(const std::vector<std::string>& methods, const std::string& pattern, Handler handler) {
        // Register first method as primary, and add duplicates for others
        if (methods.empty()) return add_route("GET", pattern, std::move(handler));
        Route& first = add_route(methods.front(), pattern, handler);
        for (size_t i = 1; i < methods.size(); ++i) {
            add_route(methods[i], pattern, handler);
        }
        return first;
    }

    Route& any(const std::string& pattern, Handler handler) {
        std::vector<std::string> methods = {"GET","POST","PUT","PATCH","DELETE","OPTIONS"};
        return match(methods, pattern, std::move(handler));
    }

    // Named routes
    Route& get(const std::string& pattern, Handler handler) {
        auto& route = add_route("GET", pattern, std::move(handler));
        return route;
    }
    // String-style controller@action overload
    Route& get(const std::string& pattern, const std::string& controller_action) {
        auto handler = handler_from_string(controller_action);
        return get(pattern, std::move(handler));
    }

    Route& post(const std::string& pattern, Handler handler) {
        auto& route = add_route("POST", pattern, std::move(handler));
        return route;
    }
    Route& post(const std::string& pattern, const std::string& controller_action) {
        auto handler = handler_from_string(controller_action);
        return post(pattern, std::move(handler));
    }

    Route& put(const std::string& pattern, Handler handler) {
        auto& route = add_route("PUT", pattern, std::move(handler));
        return route;
    }
    
    Route& patch(const std::string& pattern, Handler handler) {
        auto& route = add_route("PATCH", pattern, std::move(handler));
        return route;
    }
    
    Route& delete_(const std::string& pattern, Handler handler) {
        auto& route = add_route("DELETE", pattern, std::move(handler));
        return route;
    }
    
    Route& options(const std::string& pattern, Handler handler) {
        auto& route = add_route("OPTIONS", pattern, std::move(handler));
        return route;
    }
    Route& options(const std::string& pattern, const std::string& controller_action) {
        auto handler = handler_from_string(controller_action);
        return options(pattern, std::move(handler));
    }

    // Register a global (application) middleware - runs for every route
    void use(Middleware mw) {
        global_middlewares_.push_back(std::move(mw));
    }

    // Alias a named middleware so it can be referred to by name in groups
    void aliasMiddleware(const std::string& name, Middleware mw) {
        named_middlewares_[name] = std::move(mw);
    }

    // Convenience: register a middleware alias and also add it to global middleware by name
    void use(const std::string& middleware_name) {
        global_middleware_aliases_.push_back(middleware_name);
    }

    // Define a middleware group by name (list of middleware aliases)
    void registerMiddlewareGroup(const std::string& group_name, const std::vector<std::string>& names) {
        middleware_groups_[group_name] = names;
    }

    // Resource routing (Laravel-style)
    template<typename T>
    void resource(const std::string& prefix) {
        get(prefix, [this](const Request& req) {
            if (container_) return container_->template make<T>()->index(req);
            throw std::runtime_error("Container not set in Router");
        });
        
        get(prefix + "/{id}", [this](const Request& req) {
            if (container_) return container_->template make<T>()->show(req);
            throw std::runtime_error("Container not set in Router");
        });
        
        get(prefix + "/create", [this](const Request& req) {
            if (container_) return container_->template make<T>()->create(req);
            throw std::runtime_error("Container not set in Router");
        });
        
        post(prefix, [this](const Request& req) {
            if (container_) return container_->template make<T>()->store(req);
            throw std::runtime_error("Container not set in Router");
        });
        
        get(prefix + "/{id}/edit", [this](const Request& req) {
            if (container_) return container_->template make<T>()->edit(req);
            throw std::runtime_error("Container not set in Router");
        });
        
        put(prefix + "/{id}", [this](const Request& req) {
            if (container_) return container_->template make<T>()->update(req);
            throw std::runtime_error("Container not set in Router");
        });
        
        patch(prefix + "/{id}", [this](const Request& req) {
            if (container_) return container_->template make<T>()->update(req);
            throw std::runtime_error("Container not set in Router");
        });
        
        delete_(prefix + "/{id}", [this](const Request& req) {
            if (container_) return container_->template make<T>()->destroy(req);
            throw std::runtime_error("Container not set in Router");
        });
    }
    
    struct Attributes {
        std::string prefix;
        std::vector<Middleware> middleware;
    };

    template<typename T>
    class ControllerGroup;

    // Route groups
    class Group {
    public:
        explicit Group(Router& router, std::string  prefix = "",
                       std::vector<Middleware> middleware = {})
            : router_(router), prefix_(std::move(prefix)), middleware_(std::move(middleware)) {}

        Group& prefix(const std::string& prefix) {
            prefix_ = normalize_path(prefix_ + prefix);
            return *this;
        }
        
        Group& middleware(Middleware mw) {
            middleware_.push_back(std::move(mw));
            return *this;
        }

        // Middleware by alias name
        Group& middleware(const std::string& middleware_name) {
            // push a wrapper that resolves the named middleware at runtime from router_
            middleware_.push_back([&router = router_, middleware_name](const Request& req, const Handler& next) -> Response {
                auto it = router.named_middlewares_.find(middleware_name);
                if (it != router.named_middlewares_.end()) {
                    return it->second(req, next);
                }
                return Response::error(std::string("Named middleware not found: ") + middleware_name);
            });
            return *this;
        }

        // Middleware group by group name (expands to the registered alias list)
        Group& middleware_group(const std::string& group_name) {
            auto it = router_.middleware_groups_.find(group_name);
            if (it == router_.middleware_groups_.end()) {
                // silently ignore or optionally add an error middleware
                middleware_.push_back([group_name](const Request&, const Handler&) {
                    return Response::error(std::string("Middleware group not found: ") + group_name);
                });
                return *this;
            }

            for (const auto& alias : it->second) {
                this->middleware(alias);
            }
            return *this;
        }

        Group& group(const std::function<void(Group&)> &callback) {
            callback(*this);
            return *this;
        }

        Group& group(const Attributes& attrs, const std::function<void(Group&)>& callback) {
            Group child_group{router_, join_paths(prefix_, attrs.prefix), middleware_};
            for (const auto& mw : attrs.middleware) {
                child_group.middleware_.push_back(mw);
            }
            callback(child_group);
            return *this;
        }

        template<typename T>
        ControllerGroup<T> controller(const std::string& prefix = "");
        
        Route& get(const std::string& pattern, Handler handler) {
            return add_route("GET", pattern, std::move(handler));
        }
        
        Route& post(const std::string& pattern, Handler handler) {
            return add_route("POST", pattern, std::move(handler));
        }

        Route& put(const std::string& pattern, Handler handler) {
            return add_route("PUT", pattern, std::move(handler));
        }

        Route& patch(const std::string& pattern, Handler handler) {
            return add_route("PATCH", pattern, std::move(handler));
        }

        Route& delete_(const std::string& pattern, Handler handler) {
            return add_route("DELETE", pattern, std::move(handler));
        }

        Route& options(const std::string& pattern, Handler handler) {
            return add_route("OPTIONS", pattern, std::move(handler));
        }

        // Support controller methods in regular groups with auto-deduction
        template<typename ControllerType>
        Route& get(const std::string& pattern, Response (ControllerType::*action)(const Request&)) {
            return add_controller_route<ControllerType>("GET", pattern, action);
        }

        template<typename ControllerType>
        Route& post(const std::string& pattern, Response (ControllerType::*action)(const Request&)) {
            return add_controller_route<ControllerType>("POST", pattern, action);
        }

        template<typename ControllerType>
        Route& put(const std::string& pattern, Response (ControllerType::*action)(const Request&)) {
            return add_controller_route<ControllerType>("PUT", pattern, action);
        }

        template<typename ControllerType>
        Route& patch(const std::string& pattern, Response (ControllerType::*action)(const Request&)) {
            return add_controller_route<ControllerType>("PATCH", pattern, action);
        }

        template<typename ControllerType>
        Route& delete_(const std::string& pattern, Response (ControllerType::*action)(const Request&)) {
            return add_controller_route<ControllerType>("DELETE", pattern, action);
        }

        // Support const member functions too
        template<typename ControllerType>
        Route& get(const std::string& pattern, Response (ControllerType::*action)(const Request&) const) {
            return add_controller_route_const<ControllerType>("GET", pattern, action);
        }

        template<typename ControllerType>
        Route& post(const std::string& pattern, Response (ControllerType::*action)(const Request&) const) {
            return add_controller_route_const<ControllerType>("POST", pattern, action);
        }

    protected:
        template<typename ControllerType, typename Action>
        Route& add_controller_route(const std::string& method, const std::string& pattern, Action action) {
            // Capture a pointer to the router (long-lived) instead of 'this' (Group may be temporary)
            return this->add_route(method, pattern, [router_ptr = &router_, action](const Request& req) {
                if (!router_ptr->container_) {
                    throw std::runtime_error("Container not set in Router");
                }
                
                auto controller = router_ptr->container_->template make<ControllerType>();
                if (!controller) {
                    return Response::error("Failed to resolve controller");
                }
                
                try {
                    return (controller.get()->*action)(req);
                } catch (const std::exception& e) {
                    return Response::error(std::string("Controller action failed: ") + e.what());
                }
            });
        }

        template<typename ControllerType, typename Action>
        Route& add_controller_route_const(const std::string& method, const std::string& pattern, Action action) {
            return this->add_route(method, pattern, [router_ptr = &router_, action](const Request& req) {
                if (!router_ptr->container_) {
                    throw std::runtime_error("Container not set in Router");
                }
                
                auto controller = router_ptr->container_->template make<ControllerType>();
                if (!controller) {
                    return Response::error("Failed to resolve controller");
                }
                
                try {
                    return (controller.get()->*action)(req);
                } catch (const std::exception& e) {
                    return Response::error(std::string("Controller action failed: ") + e.what());
                }
            });
        }

        Route& add_route(const std::string& method, const std::string& pattern, Handler handler) {
            auto full_pattern = join_paths(prefix_, pattern);
            auto& route = router_.add_route(method, full_pattern, std::move(handler));
            for (const auto& mw : middleware_) {
                route.middlewares.push_back(mw);
            }
            return route;
        }

        Router& router_;
        std::string prefix_;
        std::vector<Middleware> middleware_;

        static std::string normalize_path(const std::string& path) {
            if (path.empty()) return "/";
            if (path.front() != '/') return "/" + path;
            return path;
        }
        
        static std::string join_paths(const std::string& a, const std::string& b) {
            if (a == "/") return normalize_path(b);
            if (b == "/") return a;
            return a + normalize_path(b);
        }
    };

    // Controller method routing
    template<typename T>
    class ControllerGroup : public Group {
    public:
        using Group::Group;
        
        template<typename Action>
        Route& get(const std::string& pattern, Action action) {
            return this->add_controller_route_internal("GET", pattern, action);
        }
        
        template<typename Action>
        Route& post(const std::string& pattern, Action action) {
            return this->add_controller_route_internal("POST", pattern, action);
        }
        
        template<typename Action>
        Route& put(const std::string& pattern, Action action) {
            return this->add_controller_route_internal("PUT", pattern, action);
        }
        
        template<typename Action>
        Route& patch(const std::string& pattern, Action action) {
            return this->add_controller_route_internal("PATCH", pattern, action);
        }
        
        template<typename Action>
        Route& delete_(const std::string& pattern, Action action) {
            return this->add_controller_route_internal("DELETE", pattern, action);
        }
        
        template<typename Action>
        Route& options(const std::string& pattern, Action action) {
            return this->add_controller_route_internal("OPTIONS", pattern, action);
        }

        ControllerGroup& middleware(Middleware mw) {
            Group::middleware(std::move(mw));
            return *this;
        }

        ControllerGroup& prefix(const std::string& prefix) {
            Group::prefix(prefix);
            return *this;
        }

        ControllerGroup& group(const std::function<void(ControllerGroup<T>&)>& callback) {
            callback(*this);
            return *this;
        }

    private:
        template<typename Action>
        Route& add_controller_route_internal(const std::string& method, const std::string& pattern, Action action) {
            return this->template add_controller_instance_route<T>(method, pattern, action);
        }

        // Use a different name for the base controller route adder to avoid confusion
        template<typename ControllerType, typename Action>
        Route& add_controller_instance_route(const std::string& method, const std::string& pattern, Action action) {
            return this->add_route(method, pattern, [router_ptr = &router_, action](const Request& req) {
                if (router_ptr->container_) {
                    auto controller = router_ptr->container_->template make<ControllerType>();
                    if (!controller) {
                         return Response::error("Failed to resolve controller");
                    }
                    return (controller.get()->*action)(req);
                }
                throw std::runtime_error("Container not set in Router");
            });
        }
    };
    
    template<typename T>
    ControllerGroup<T> controller(const std::string& prefix) {
        return ControllerGroup<T>(*this, prefix);
    }
    
    Group group(const std::string& prefix = "", std::vector<Middleware> middleware = {}) {
        return Group{*this, prefix, std::move(middleware)};
    }

    void group(const Attributes& attrs, const std::function<void(Group&)>& callback) {
        Group g{*this, attrs.prefix, attrs.middleware};
        callback(g);
    }
    
    // Dispatch request
    [[nodiscard]] Response dispatch(const Request& request) const {
        for (const auto& route : routes_) {
            // Skip routes that don't match method/path
            if (!route.matches(request.method(), request.path())) {
                continue;
            }

            // Extract path parameters
            auto params = route.extract_params(request.path());
            Request modified_request = request;
            for (const auto& [key, value] : params) {
                modified_request.set_param(key, value);
            }

            // Apply middleware
            // Combine global middlewares, resolved global alias middlewares, and route-specific middlewares
            // Global middlewares run first.
            std::vector<Middleware> combined;
            combined.reserve(global_middlewares_.size() + global_middleware_aliases_.size() +
                             route.middlewares.size() + route.middleware_aliases.size());

            // explicit function middlewares
            for (const auto& gmw : global_middlewares_) combined.push_back(gmw);

            // resolve global aliases
            for (const auto& alias : global_middleware_aliases_) {
                auto it = named_middlewares_.find(alias);
                if (it != named_middlewares_.end()) combined.push_back(it->second);
                else combined.push_back([alias](const Request&, const Handler&) {
                    return Response::error(std::string("Named middleware not found (global): ") + alias);
                });
            }

            // resolve route aliases
            for (const auto& alias : route.middleware_aliases) {
                auto it = named_middlewares_.find(alias);
                if (it != named_middlewares_.end()) combined.push_back(it->second);
                else combined.push_back([alias](const Request&, const Handler&) {
                    return Response::error(std::string("Named middleware not found (route): ") + alias);
                });
            }

            // then explicit per-route middleware objects
            for (const auto& rmw : route.middlewares) combined.push_back(rmw);

            if (combined.empty()) {
                return route.handler(modified_request);
            }

            // Build middleware pipeline (apply in-order so first in combined runs first)
            Handler final_handler = route.handler;
            for (const auto & middleware : std::ranges::reverse_view(combined)) {
                final_handler = [mw = middleware, next = std::move(final_handler)](const Request& req) {
                    return mw(req, next);
                };
            }

            return final_handler(modified_request);
        }
        
        return Response::not_found();
    }
    
    // URL generation (for named routes)
    [[nodiscard]] std::string route(const std::string& name,
                     const std::unordered_map<std::string, std::string>& params = {}) const {
        for (const auto& route : routes_) {
            if (route.route_name == name) {
                std::string path = route_to_path(route, params);
                return path;
            }
        }
        return "";
    }

    // alias for Laravel-like naming
    [[nodiscard]] std::string urlFor(const std::string& name,
                     const std::unordered_map<std::string, std::string>& params = {}) const {
        return route(name, params);
    }

    void set_container(breeze::core::Container* container) {
        container_ = container;
    }

    // Controller registry to support string-style "Controller@action" routing
    struct ControllerDescriptor {
        std::function<std::shared_ptr<breeze::http::Controller>()> factory;
        std::unordered_map<std::string, std::function<Response(std::shared_ptr<breeze::http::Controller>, const Request&)>> actions;
    };

    template<typename ControllerType>
    void register_controller(const std::string& name) {
        controller_descriptors_[name].factory = [this]() -> std::shared_ptr<breeze::http::Controller> {
            if (!container_) return nullptr;
            return container_->template make<ControllerType>();
        };
    }

    template<typename ControllerType>
    void register_controller_action(const std::string& controller_name, const std::string& action_name, Response (ControllerType::*method)(const Request&)) {
        controller_descriptors_[controller_name].actions[action_name] = [method](std::shared_ptr<breeze::http::Controller> c, const Request& req) -> Response {
            auto typed = std::static_pointer_cast<ControllerType>(c);
            return (typed.get()->*method)(req);
        };
    }

    Handler handler_from_string(const std::string& spec) {
        // Accept several separators: '@', '::', or '.' for convenience
        std::string controller;
        std::string action;

        auto pos_at = spec.find('@');
        if (pos_at != std::string::npos) {
            controller = spec.substr(0, pos_at);
            action = spec.substr(pos_at + 1);
        } else {
            // try C++ style Class::method
            auto pos_colon = spec.find("::");
            if (pos_colon != std::string::npos) {
                controller = spec.substr(0, pos_colon);
                action = spec.substr(pos_colon + 2);
            } else {
                // try dot notation Controller.action
                auto pos_dot = spec.find('.');
                if (pos_dot != std::string::npos) {
                    controller = spec.substr(0, pos_dot);
                    action = spec.substr(pos_dot + 1);
                } else {
                    return [spec](const Request&) {
                        return Response::error(std::string("Invalid controller action spec: ") + spec);
                    };
                }
            }
        }

        // Trim whitespace
        auto trim = [](std::string s) {
            size_t start = 0;
            while (start < s.size() && std::isspace((unsigned char)s[start])) ++start;
            size_t end = s.size();
            while (end > start && std::isspace((unsigned char)s[end-1])) --end;
            return s.substr(start, end - start);
        };

        controller = trim(controller);
        action = trim(action);

        return [this, controller = std::move(controller), action = std::move(action)](const Request& req) {
            auto it = controller_descriptors_.find(controller);
            if (it == controller_descriptors_.end()) {
                return Response::error(std::string("Controller not registered: ") + controller);
            }
            auto& desc = it->second;
            if (!desc.factory) {
                return Response::error(std::string("Controller factory not set: ") + controller);
            }
            auto inst = desc.factory();
            if (!inst) {
                return Response::error(std::string("Failed to resolve controller: ") + controller);
            }
            auto ait = desc.actions.find(action);
            if (ait == desc.actions.end()) {
                return Response::error(std::string("Controller action not found: ") + action);
            }
            return ait->second(inst, req);
        };
    }

private:
    [[nodiscard]] static std::pair<std::vector<std::string>, std::string> compile_pattern(const std::string& pattern) {
        std::vector<std::string> param_names;
        std::string regex_pattern;

        std::istringstream stream(pattern);
        std::string segment;

        // Start with ^
        regex_pattern = "^";

        while (std::getline(stream, segment, '/')) {
            if (segment.empty()) continue;

            if (segment.front() == '{' && segment.back() == '}') {
                // Extract parameter name (remove { and })
                std::string param_name = segment.substr(1, segment.size() - 2);
                param_names.push_back(param_name);
                regex_pattern += "/([^/]+)";
            } else {
                regex_pattern += "/" + segment;
            }
        }

        // Handle root path
        if (regex_pattern == "^") {
            regex_pattern = "^/$";
        } else {
            // Make trailing slash optional for non-root patterns
            regex_pattern += "/?$";
        }

        return {param_names, regex_pattern};
    }
    
    [[nodiscard]] static std::string route_to_path(const Route& route,
                                                   const std::unordered_map<std::string, std::string>& params) {
        // Use the original pattern string and substitute {name} with provided params
        std::string out = route.pattern_str;
        // Ensure leading slash
        if (out.empty() || out.front() != '/') out = "/" + out;

        // Replace occurrences of {param}
        for (const auto& [k, v] : params) {
            std::string token = "{" + k + "}";
            size_t pos = 0;
            while ((pos = out.find(token, pos)) != std::string::npos) {
                out.replace(pos, token.length(), v);
                pos += v.length();
            }
        }

        // Remove any unresolved tokens (best-effort)
        size_t open;
        while ((open = out.find('{')) != std::string::npos) {
            size_t close = out.find('}', open);
            if (close == std::string::npos) break;
            out.erase(open, close - open + 1);
        }

        // Normalize trailing slash: do not force a trailing slash unless root
        if (out.size() > 1 && out.back() == '/') out.pop_back();

        return out;
    }

    std::vector<Route> routes_;
    breeze::core::Container* container_ = nullptr;

    // Global middlewares applied to every route (in-order)
    std::vector<Middleware> global_middlewares_;
    std::vector<std::string> global_middleware_aliases_;

    // Named middleware aliases and groups
    std::unordered_map<std::string, Middleware> named_middlewares_;
    std::unordered_map<std::string, std::vector<std::string>> middleware_groups_;

    // Controller registry for string-style routing
    std::unordered_map<std::string, ControllerDescriptor> controller_descriptors_;

    // Make Route accessible for testing
    #ifdef TESTING
    const std::vector<Route>& get_routes() const { return routes_; }
    #endif
    };
} // namespace breeze::http

template<typename T>
inline breeze::http::Router::ControllerGroup<T> breeze::http::Router::Group::controller(const std::string& prefix) {
    return ControllerGroup<T>(router_, join_paths(prefix_, prefix), middleware_);
}

