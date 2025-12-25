#pragma once

#include <breeze/http/middleware.hpp>
#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>
#include <breeze/http/router.hpp>

namespace breeze::core {

class Kernel {
public:
    Kernel() { breeze::http::Route::set_router(router_); }

    breeze::http::Router& router() { return router_; }
    const breeze::http::Router& router() const { return router_; }

    breeze::http::MiddlewarePipeline& middleware() { return middleware_; }
    const breeze::http::MiddlewarePipeline& middleware() const { return middleware_; }

    breeze::http::Response handle(const breeze::http::Request& request) const;

private:
    breeze::http::Router router_;
    breeze::http::MiddlewarePipeline middleware_;
};

} // namespace breeze::core
