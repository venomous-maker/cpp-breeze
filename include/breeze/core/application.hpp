#pragma once

#include <breeze/core/config.hpp>
#include <breeze/core/container.hpp>
#include <breeze/core/kernel.hpp>
#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>

namespace breeze::core {

class Application {
public:
    Container& container() { return container_; }
    const Container& container() const { return container_; }

    Config& config() { return config_; }
    const Config& config() const { return config_; }

    Kernel& kernel() { return kernel_; }
    const Kernel& kernel() const { return kernel_; }

    breeze::http::Response handle(const breeze::http::Request& request) const { return kernel_.handle(request); }

private:
    Container container_;
    Config config_;
    Kernel kernel_;
};

} // namespace breeze::core
