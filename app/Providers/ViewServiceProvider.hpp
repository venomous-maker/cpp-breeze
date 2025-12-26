// app/Providers/ViewServiceProvider.hpp
#pragma once
#include <breeze/core/application.hpp>
#include <breeze/support/view.hpp>

namespace app::Providers {

class ViewServiceProvider : public breeze::core::ServiceProvider {
public:
    using breeze::core::ServiceProvider::ServiceProvider;

    void register_services() override {
        app_.container().singleton<breeze::support::View>([this]() {
            // Get views path from config or default to resources/views
            std::string views_path = app_.config().get("view.paths", "resources/views");
            return std::make_shared<breeze::support::View>(views_path);
        });
    }

    void boot() override {
        // Any boot logic for view engine
    }
};

} // namespace app::Providers
