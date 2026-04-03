#include <breeze/database/connection/utils.hpp>

#include <sstream>

namespace breeze::database::utils {

std::string valueToString(const Value& value)
{
    return std::visit([](auto&& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return std::string();
        } else if constexpr (std::is_same_v<T, int> || std::is_same_v<T, int64_t>) {
            return std::to_string(v);
        } else if constexpr (std::is_same_v<T, double>) {
            std::ostringstream oss; oss << v; return oss.str();
        } else if constexpr (std::is_same_v<T, bool>) {
            return v ? "1" : "0";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return v;
        } else if constexpr (std::is_same_v<T, std::vector<char>>) {
            return std::string(v.begin(), v.end());
        } else {
            return std::string();
        }
    }, value);
}

Value stringToValue(const std::string& str, const std::string& type)
{
    if (type == "int") {
        try { return std::stoi(str); } catch (...) { return 0; }
    }
    if (type == "int64") {
        try { return static_cast<int64_t>(std::stoll(str)); } catch (...) { return static_cast<int64_t>(0); }
    }
    if (type == "double") {
        try { return std::stod(str); } catch (...) { return 0.0; }
    }
    if (type == "bool") {
        std::string s = str;
        for (auto &c : s) c = static_cast<char>(::tolower(c));
        return (s == "1" || s == "true" || s == "yes");
    }
    if (type == "blob") {
        return std::vector<char>(str.begin(), str.end());
    }
    // default to string
    return str;
}

} // namespace breeze::database::utils

