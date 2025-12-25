#pragma once

#include <string>
#include <string_view>

namespace breeze::support::str {

std::string trim(std::string_view s);
std::string to_lower(std::string_view s);
std::string replace_all(std::string_view s, std::string_view from, std::string_view to);

} // namespace breeze::support::str
