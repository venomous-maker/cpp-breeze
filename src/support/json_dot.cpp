#include <breeze/support/json_dot.hpp>
#include <sstream>
#include <cctype>

namespace breeze::support {

static bool is_number(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit((unsigned char)c)) return false;
    return true;
}

bool json_get_dot(const nlohmann::json& root, const std::string& path, nlohmann::json& out) {
    if (path.empty()) { out = root; return true; }
    const nlohmann::json* cur = &root;
    size_t i = 0;
    const size_t n = path.size();

    while (i < n) {
        // extract next segment, supporting escaped characters (e.g., "a\.b" -> key "a.b")
        std::string seg;
        bool saw_escape = false;
        while (i < n) {
            char c = path[i];
            if (c == '\\' && (i + 1) < n) {
                // escape next character
                seg.push_back(path[i + 1]);
                saw_escape = true;
                i += 2;
                continue;
            }
            if (c == '.') {
                ++i; // consume dot separator
                break;
            }
            seg.push_back(c);
            ++i;
        }

        if (seg.empty()) return false; // malformed

        if (is_number(seg) && !saw_escape) {
            // numeric-only segment (and not escaped) -> array index
            if (!cur->is_array()) return false;
            size_t idx = 0;
            try { idx = static_cast<size_t>(std::stoul(seg)); }
            catch (...) { return false; }
            if (idx >= cur->size()) return false;
            cur = &((*cur)[idx]);
        } else {
            // object key (supports keys containing dots via escaping: e.g., "a\.b")
            if (!cur->is_object()) return false;
            auto it = cur->find(seg);
            if (it == cur->end()) return false;
            cur = &(*it);
        }
    }

    out = *cur;
    return true;
}

nlohmann::json json_get_dot_or(const nlohmann::json& root, const std::string& path, nlohmann::json default_value) {
    nlohmann::json out;
    if (json_get_dot(root, path, out)) return out;
    return default_value;
}

} // namespace breeze::support
