#include <breeze/support/view.hpp>
#include <breeze/support/blade.hpp>

namespace breeze::support {

View::View(std::filesystem::path views_path) : views_path_(std::move(views_path)) {}

std::string View::render(const std::string& template_name, const nlohmann::json& data) {
    // Prefer the .breeze extension; fallback to other common template extensions.
    const std::vector<std::string> exts = {".breeze", ".page", ".html", ".htm", ".chtm"};

    std::filesystem::path full_path;
    bool found = false;
    for (const auto& ext : exts) {
        std::filesystem::path p = views_path_ / (template_name + ext);
        if (std::filesystem::exists(p)) {
            full_path = std::move(p);
            found = true;
            break;
        }
    }

    if (!found) {
        return "View [" + template_name + "] not found in " + views_path_.string();
    }

    Blade blade;
    return blade.render_from_file(full_path, data);
}

} // namespace breeze::support
