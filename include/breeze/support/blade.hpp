#pragma once

#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
#include <filesystem>

namespace breeze::support {

class Blade {
public:
    // Render a template string at runtime using a JSON context. Supports
    // directives: @foreach(... as ...), @if(...), @unless(...), and {{ var }}.
    [[nodiscard]] std::string render(std::string_view tpl, const nlohmann::json& context) const;

    // Render directly from a template file path. Uses a file-based cache keyed by
    // file path + mtime with LRU eviction and optional TTL.
    [[nodiscard]] std::string render_from_file(const std::filesystem::path& file_path, const nlohmann::json& context) const;
};

} // namespace breeze::support
