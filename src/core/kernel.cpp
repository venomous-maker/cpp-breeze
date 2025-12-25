#include <breeze/core/kernel.hpp>

namespace breeze::core {

breeze::http::Response Kernel::handle(const breeze::http::Request& request) const
{
    return middleware_.run(request, [this](const breeze::http::Request& req) {
        return router_.dispatch(req);
    });
}

} // namespace breeze::core
