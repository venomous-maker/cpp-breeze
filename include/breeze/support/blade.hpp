#pragma once

#include <string>
#include <string_view>
#include <nlohmann/json.hpp>

namespace breeze::support {

class Blade {
public:
    // Render a template string at runtime using a JSON context. Supports
    // directives: @foreach(... as ...), @if(...), @unless(...), and {{ var }}.
    [[nodiscard]] std::string render(std::string_view tpl, const nlohmann::json& context) const;
};

} // namespace breeze::support
