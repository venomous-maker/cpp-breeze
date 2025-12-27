#include <breeze/http/response.hpp>
#include <breeze/core/application.hpp>
#include <breeze/support/view.hpp>
#include <iostream>

namespace breeze::http {

Response Response::view(const std::string& template_name, const nlohmann::json& data) {
    if (!breeze::core::Application::has_instance()) {
        return Response::error("Application instance not initialized");
    }
    auto& app = breeze::core::Application::instance();
    auto view_engine = app.container().make<breeze::support::View>();

    if (!view_engine) {
        return Response::error("View engine not found in container");
    }
    
    Response res{StatusCode::OK};
    res.content_type("text/html");
    res.set_body(view_engine->render(template_name, data));
    return res;
}

} // namespace breeze::http
