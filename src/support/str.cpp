#include <breeze/support/str.hpp>

#include <algorithm>
#include <cctype>

namespace breeze::support::str {

std::string trim(std::string_view s)
{
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }

    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }

    return std::string{s.substr(start, end - start)};
}

std::string to_lower(std::string_view s)
{
    std::string out{s};
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string replace_all(std::string_view s, std::string_view from, std::string_view to)
{
    if (from.empty()) {
        return std::string{s};
    }

    std::string out;
    out.reserve(s.size());

    std::size_t i = 0;
    while (i < s.size()) {
        if (i + from.size() <= s.size() && s.substr(i, from.size()) == from) {
            out.append(to);
            i += from.size();
        } else {
            out.push_back(s[i]);
            ++i;
        }
    }

    return out;
}

} // namespace breeze::support::str
