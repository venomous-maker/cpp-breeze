#include <breeze/support/blade.hpp>

#include <breeze/support/str.hpp>

namespace breeze::support {

std::string Blade::render(std::string_view tpl, const std::unordered_map<std::string, std::string>& context) const
{
    std::string out{tpl};
    for (const auto& [k, v] : context) {
        const std::string needle = "{{" + k + "}}";
        out = breeze::support::str::replace_all(out, needle, v);
    }
    return out;
}

} // namespace breeze::support
