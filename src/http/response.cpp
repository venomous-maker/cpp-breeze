#include <breeze/http/response.hpp>
#include <breeze/core/application.hpp>
#include <breeze/support/view.hpp>

namespace breeze::http {

Response Response::view(const std::string& template_name, const nlohmann::json& data) {
    auto view_engine = breeze::core::Application::instance().container().make<breeze::support::View>();
    
    Response res{StatusCode::OK};
    res.content_type("text/html");
    res.set_body(view_engine->render(template_name, data));
    return res;
}

} // namespace breeze::http
