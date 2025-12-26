// include/breeze/http/router.hpp
#pragma once
#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>
#include <breeze/http/middleware.hpp>
#include <breeze/core/container.hpp>
#include <functional>
#include <memory>
#include <ranges>
#include <regex>
#include <string>
#include <unordered_map>
#include <utility>
#include <utility>
#include <vector>
#include <type_traits>
#include <optional>

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
        
        [[nodiscard]] bool matches(const std::string& method, const std::string& path) const {
            return this->method == method && std::regex_match(path, pattern);
        }
        
        [[nodiscard]] std::unordered_map<std::string, std::string> extract_params(const std::string& path) const {
            std::unordered_map<std::string, std::string> params;
            std::smatch matches;
            
            if (std::regex_match(path, matches, pattern)) {
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
            {}  // middleware
        });
        
        return routes_.back();
    }
    
    // Named routes
    Route& get(const std::string& pattern, Handler handler) {
        auto& route = add_route("GET", pattern, std::move(handler));
        return route;
    }
    
    Route& post(const std::string& pattern, Handler handler) {
        auto& route = add_route("POST", pattern, std::move(handler));
        return route;
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
        
        Group& group(const std::function<void(Group&)> &callback) {
            callback(*this);
            return *this;
        }
        
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

    protected:
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
            return add_controller_route("GET", pattern, action);
        }
        
        template<typename Action>
        Route& post(const std::string& pattern, Action action) {
            return add_controller_route("POST", pattern, action);
        }
        
        template<typename Action>
        Route& put(const std::string& pattern, Action action) {
            return add_controller_route("PUT", pattern, action);
        }
        
        template<typename Action>
        Route& patch(const std::string& pattern, Action action) {
            return add_controller_route("PATCH", pattern, action);
        }
        
        template<typename Action>
        Route& delete_(const std::string& pattern, Action action) {
            return add_controller_route("DELETE", pattern, action);
        }
        
        template<typename Action>
        Route& options(const std::string& pattern, Action action) {
            return add_controller_route("OPTIONS", pattern, action);
        }

        ControllerGroup& middleware(Middleware mw) {
            Group::middleware(std::move(mw));
            return *this;
        }

        ControllerGroup& prefix(const std::string& prefix) {
            Group::prefix(prefix);
            return *this;
        }

        ControllerGroup& group(const std::function<void(ControllerGroup&)>& callback) {
            callback(*this);
            return *this;
        }

    private:
        template<typename Action>
        Route& add_controller_route(const std::string& method, const std::string& pattern, Action action) {
            return this->add_route(method, pattern, [this, action](const Request& req) {
                // We need access to the container.
                // Since we don't have it easily here, let's look at how we can get it.
                // Router is owned by Kernel, which is owned by Application.
                // Maybe we can pass the container to the Router.
                if (router_.container_) {
                    auto controller = router_.container_->template make<T>();
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
    
    // Dispatch request
    [[nodiscard]] Response dispatch(const Request& request) const {
        for (const auto& route : routes_) {
            if (route.matches(request.method(), request.path())) {
                // Extract path parameters
                auto params = route.extract_params(request.path());
                Request modified_request = request;
                for (const auto& [key, value] : params) {
                    modified_request.set_param(key, value);
                }
                
                // Apply middleware
                if (route.middlewares.empty()) {
                    return route.handler(modified_request);
                }
                
                // Build middleware pipeline
                Handler final_handler = route.handler;
                for (const auto & middleware : std::ranges::reverse_view(route.middlewares)) {
                    final_handler = [mw = middleware, next = std::move(final_handler)](const Request& req) {
                        return mw(req, next);
                    };
                }
                
                return final_handler(modified_request);
            }
        }
        
        return Response::not_found();
    }
    
    // URL generation (for named routes)
    [[nodiscard]] std::string route(const std::string& name,
                     const std::unordered_map<std::string, std::string>& params = {}) const {
        for (const auto& route : routes_) {
            if (route.route_name == name) {
                std::string path = route_to_path(route.pattern, params);
                return path;
            }
        }
        return "";
    }
    
    void set_container(breeze::core::Container* container) {
        container_ = container;
    }

private:
    [[nodiscard]] static std::pair<std::vector<std::string>, std::string> compile_pattern(const std::string& pattern) {
        std::vector<std::string> param_names;
        std::string regex_pattern;
        
        std::istringstream stream(pattern);
        std::string segment;
        
        while (std::getline(stream, segment, '/')) {
            if (segment.empty()) continue;
            
            if (segment.front() == '{' && segment.back() == '}') {
                std::string param_name = segment.substr(1, segment.size() - 2);
                param_names.push_back(param_name);
                regex_pattern += "/([^/]+)";
            } else {
                regex_pattern += "/" + segment;
            }
        }
        
        if (regex_pattern.empty()) {
            regex_pattern = "/";
        }
        
        regex_pattern = "^" + regex_pattern + "$";
        
        return {param_names, regex_pattern};
    }
    
    [[nodiscard]] static std::string route_to_path(const std::regex& pattern,
                                                   const std::unordered_map<std::string, std::string>& params) {
        // Simplified implementation - in reality would need to reverse engineer the pattern
        return "";
    }
    
    std::vector<Route> routes_;
    breeze::core::Container* container_ = nullptr;
    
    // Make Route accessible for testing
    #ifdef TESTING
    const std::vector<Route>& get_routes() const { return routes_; }
    #endif
};
} // namespace breeze::http