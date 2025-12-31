// app/Providers/ViewServiceProvider.hpp
#pragma once
#include <breeze/core/application.hpp>
#include <breeze/support/view.hpp>
#include <breeze/support/view_engine.hpp>
#include <breeze/http/response.hpp>

namespace app::Providers {

class ViewServiceProvider : public breeze::core::ServiceProvider {
public:
    using breeze::core::ServiceProvider::ServiceProvider;

    void register_services() override {
        // Bind the concrete View as the default IViewEngine implementation
        app_.container().singleton<breeze::support::IViewEngine>([this]() {
            // Get views path from config or default to resources/views
            std::string views_path = app_.config().get("view.paths", "resources/views");
            return std::make_shared<breeze::support::View>(views_path);
        });

        // Also keep the concrete View registered for code that requests it specifically
        app_.container().singleton<breeze::support::View>([this]() {
            std::string views_path = app_.config().get("view.paths", "resources/views");
            return std::make_shared<breeze::support::View>(views_path);
        });
    }

    void boot() override {
        // Register Response view renderer to use the container's IViewEngine
        try {
            if (app_.container().can_make<breeze::support::IViewEngine>()) {
                auto engine = app_.container().make<breeze::support::IViewEngine>();
                breeze::http::Response::set_view_renderer([engine](const std::string& tpl, const nlohmann::json& data) {
                    return engine->render(tpl, data);
                });
            }
        } catch (...) {
            // ignore failures during boot
        }
    }
};

} // namespace app::Providers
