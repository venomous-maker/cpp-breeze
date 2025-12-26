#include <breeze/breeze.hpp>

void register_api_routes(breeze::core::Application& app) {
    auto& router = app.kernel().router();

    router.group("/api").group([&app](auto& group) {
        group.get("/user", [](const breeze::http::Request&) {
            return breeze::http::Response::json({{"name", "John Doe"}});
        });

        group.get("/status", [](const breeze::http::Request&) {
            return breeze::http::Response::json({{"status", "ok"}, {"version", "1.0.0"}});
        });

        group.get("/config", [&app](const breeze::http::Request&) {
            return breeze::http::Response::json({
                {"app_name", app.config().get("app.name")},
                {"env", app.config().get("app.env")}
            });
        });
    });
}
