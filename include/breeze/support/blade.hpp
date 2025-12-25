#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace breeze::support {

class Blade {
public:
    std::string render(std::string_view tpl, const std::unordered_map<std::string, std::string>& context) const;
};

} // namespace breeze::support
