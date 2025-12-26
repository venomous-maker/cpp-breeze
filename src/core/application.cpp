#include <breeze/core/application.hpp>
#include <filesystem>
#include <iostream>

namespace breeze::core {

void Application::finalize_routing() {
    kernel_.router().set_container(&container_);
}

} // namespace breeze::core
