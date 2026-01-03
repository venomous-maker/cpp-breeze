#pragma once

#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>

#include <functional>
#include <ranges>
#include <vector>

namespace breeze::http {

class MiddlewarePipeline {
public:
    using Next = std::function<Response(const Request&)>;
    using Middleware = std::function<Response(const Request&, Next)>;

    void add(Middleware mw) { middlewares_.push_back(std::move(mw)); }

    [[nodiscard]] Response run(const Request& request, Next last) const
    {
        Next next = std::move(last);
        for (auto mw : std::ranges::reverse_view(middlewares_)) {
            Next prev = std::move(next);
            next = [mw = std::move(mw), prev = std::move(prev)](const Request& req) {
                return mw(req, prev);
            };
        }
        return next(request);
    }

private:
    std::vector<Middleware> middlewares_;
};

} // namespace breeze::http
