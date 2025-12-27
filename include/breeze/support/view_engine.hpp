#pragma once

#include <string>
#include <nlohmann/json.hpp>

namespace breeze::support {

class IViewEngine {
public:
    virtual ~IViewEngine() = default;
    virtual std::string render(const std::string& template_name, const nlohmann::json& data) = 0;
};

} // namespace breeze::support

