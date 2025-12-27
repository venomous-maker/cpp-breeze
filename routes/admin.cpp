#include <breeze/breeze.hpp>

void register_admin_routes(breeze::core::Application& app) {
    auto& router = app.kernel().router();
    router.group({.prefix = "/admin"}, [&](auto& group) {
        group.get("/blade/cache", [&](const breeze::http::Request&) {
            auto stats = breeze::support::Blade::cache_stats();
            return breeze::http::Response::json(stats);
        });

        group.post("/blade/clear", [&](const breeze::http::Request&) {
            breeze::support::Blade::clear_cache();
            return breeze::http::Response::json({{"status", "ok"}, {"message", "Blade cache cleared"}});
        });
    });
}

