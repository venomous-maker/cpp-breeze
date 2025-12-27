#include <breeze/core/kernel.hpp>
#include <iostream>

namespace breeze::core {

breeze::http::Response Kernel::handle(const breeze::http::Request& request) const
{
    try {
        // Run the middleware pipeline (which may call router_.dispatch)
        auto res = middleware_.run(request, [this](const breeze::http::Request& req) {
            return router_.dispatch(req);
        });

        // Mandatory logging for every request (same format as RequestLogger)
        std::string method = request.method();
        std::string path = request.path();
        std::string ip = request.header("x-remote-addr", "unknown");
        int status = static_cast<int>(res.status());
        std::cout << "[Request] " << method << " " << path << " - " << ip << " - " << status << std::endl;

        return res;
    } catch (const std::exception& e) {
        std::cout << "[Request] " << request.method() << " " << request.path() << " - " << request.header("x-remote-addr", "unknown") << " - " << "500 (exception: " << e.what() << ")" << std::endl;
        return breeze::http::Response::error(std::string("Kernel failed: ") + e.what());
    }
}

} // namespace breeze::core
