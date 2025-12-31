#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace breeze::support {

// Retrieve a value from JSON using dot-notation, e.g. "a.b.0.name".
// Returns true and sets 'out' when found; returns false and leaves 'out' unchanged otherwise.
bool json_get_dot(const nlohmann::json& root, const std::string& path, nlohmann::json& out);

// Convenience: return the value at path or a provided default (by value).
nlohmann::json json_get_dot_or(const nlohmann::json& root, const std::string& path, nlohmann::json default_value = nullptr);

} // namespace breeze::support

