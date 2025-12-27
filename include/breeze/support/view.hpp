// include/breeze/support/view.hpp
#pragma once
#include <string>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <breeze/support/view_engine.hpp>

namespace breeze::support {

class View : public IViewEngine {
public:
    explicit View(std::filesystem::path views_path);

    std::string render(const std::string& template_name, const nlohmann::json& data) override;

private:
    std::filesystem::path views_path_;
};

} // namespace breeze::support
