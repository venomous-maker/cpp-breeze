// include/breeze/support/view.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <vector>

namespace breeze::support {

class View {
public:
    View(const std::filesystem::path& views_path);

    std::string render(const std::string& template_name, const nlohmann::json& data = {});

private:
    std::string parse(std::string content, const nlohmann::json& data);
    std::string resolve_data(const std::string& key, const nlohmann::json& data);
    bool is_truthy(const std::string& key, const nlohmann::json& data);
    std::vector<std::string> split(const std::string& s, char delimiter);

    std::filesystem::path views_path_;
};

} // namespace breeze::support
