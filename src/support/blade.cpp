#include <breeze/support/blade.hpp>

#include <regex>
#include <sstream>
#include <cctype>

namespace breeze::support {

static std::vector<std::string> split_dot(const std::string& s) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, '.')) {
        tokens.push_back(token);
    }
    return tokens;
}

static std::string resolve_data(const std::string& key, const nlohmann::json& data) {
    auto parts = split_dot(key);
    const nlohmann::json* current = &data;

    for (const auto& part : parts) {
        if (current->contains(part)) {
            current = &((*current)[part]);
        } else {
            return "";
        }
    }

    if (current->is_string()) return current->get<std::string>();
    if (current->is_number()) return current->dump();
    if (current->is_boolean()) return current->get<bool>() ? "true" : "false";
    return "";
}


// Helper to return a pointer to a json value for a dotted identifier, or nullptr.
static const nlohmann::json* get_json_ptr(const std::string& key, const nlohmann::json& data) {
    auto parts = split_dot(key);
    const nlohmann::json* current = &data;

    for (const auto& part : parts) {
        if (current->contains(part)) {
            current = &((*current)[part]);
        } else {
            return nullptr;
        }
    }
    return current;
}

static bool is_truthy_value(const nlohmann::json& current) {
    if (current.is_boolean()) return current.get<bool>();
    if (current.is_null()) return false;
    if (current.is_string()) return !current.get<std::string>().empty();
    if (current.is_number()) return current.get<double>() != 0;
    if (current.is_array() || current.is_object()) return !current.empty();
    return true;
}

// Simple expression evaluator supporting identifiers, numeric/string/boolean literals,
// comparison operators (==, !=, >, >=, <, <=), and logical &&, ||, unary !, and parentheses.
class ExprParser {
public:
    ExprParser(const std::string& s, const nlohmann::json& ctx) : str_(s), pos_(0), ctx_(ctx) {}

    bool parse() {
        skip_ws();
        auto v = parse_or();
        skip_ws();
        return is_truthy_value(v);
    }

private:
    std::string str_;
    size_t pos_;
    const nlohmann::json& ctx_;

    void skip_ws() {
        while (pos_ < str_.size() && std::isspace(static_cast<unsigned char>(str_[pos_]))) ++pos_;
    }

    bool starts_with(const std::string& t) {
        return str_.substr(pos_, t.size()) == t;
    }

    nlohmann::json parse_or() {
        auto left = parse_and();
        skip_ws();
        while (starts_with("||")) {
            pos_ += 2; skip_ws();
            auto right = parse_and();
            bool l = is_truthy_value(left);
            bool r = is_truthy_value(right);
            left = (l || r);
            skip_ws();
        }
        return left;
    }

    nlohmann::json parse_and() {
        auto left = parse_comparison();
        skip_ws();
        while (starts_with("&&")) {
            pos_ += 2; skip_ws();
            auto right = parse_comparison();
            bool l = is_truthy_value(left);
            bool r = is_truthy_value(right);
            left = (l && r);
            skip_ws();
        }
        return left;
    }

    nlohmann::json parse_comparison() {
        auto left = parse_unary();
        skip_ws();

        // comparison operators
        const std::vector<std::string> ops = {"==","!=",">=","<=",">","<"};
        for (auto& op : ops) {
            if (starts_with(op)) {
                pos_ += op.size(); skip_ws();
                auto right = parse_unary();
                return compare_values(left, op, right);
            }
        }
        return left;
    }

    nlohmann::json parse_unary() {
        skip_ws();
        if (starts_with("!")) {
            ++pos_; skip_ws();
            auto v = parse_unary();
            bool truth = is_truthy_value(v);
            return !truth;
        }
        return parse_primary();
    }

    nlohmann::json parse_primary() {
        skip_ws();
        if (pos_ >= str_.size()) return nullptr;
        if (str_[pos_] == '(') {
            ++pos_; skip_ws();
            auto v = parse_or();
            skip_ws(); if (pos_ < str_.size() && str_[pos_] == ')') ++pos_;
            return v;
        }

        // string literal
        if (str_[pos_] == '"' || str_[pos_] == '\'') {
            char quote = str_[pos_++];
            std::string out;
            while (pos_ < str_.size() && str_[pos_] != quote) {
                if (str_[pos_] == '\\' && pos_ + 1 < str_.size()) {
                    out.push_back(str_[pos_ + 1]); pos_ += 2; continue;
                }
                out.push_back(str_[pos_++]);
            }
            if (pos_ < str_.size() && str_[pos_] == quote) ++pos_;
            return out;
        }

        // identifier or literal (number, true, false, null)
        if (std::isalpha(static_cast<unsigned char>(str_[pos_])) || str_[pos_] == '_') {
            size_t start = pos_;
            while (pos_ < str_.size() && (std::isalnum(static_cast<unsigned char>(str_[pos_])) || str_[pos_] == '_' || str_[pos_] == '.')) ++pos_;
            std::string tok = str_.substr(start, pos_ - start);
            if (tok == "true") return true;
            if (tok == "false") return false;
            if (tok == "null") return nullptr;
            // identifier: look up json value
            if (auto p = get_json_ptr(tok, ctx_); p) return *p;
            return nullptr;
        }

        // number
        if (std::isdigit(static_cast<unsigned char>(str_[pos_])) || (str_[pos_] == '-' && pos_ + 1 < str_.size() && std::isdigit(static_cast<unsigned char>(str_[pos_ + 1])))) {
            size_t start = pos_;
            if (str_[pos_] == '-') ++pos_;
            while (pos_ < str_.size() && std::isdigit(static_cast<unsigned char>(str_[pos_]))) ++pos_;
            if (pos_ < str_.size() && str_[pos_] == '.') {
                ++pos_;
                while (pos_ < str_.size() && std::isdigit(static_cast<unsigned char>(str_[pos_]))) ++pos_;
            }
            std::string num = str_.substr(start, pos_ - start);
            try { return std::stod(num); } catch (...) { return nullptr; }
        }

        // nothing matched
        return nullptr;
    }

    // Compare two json values according to operator
    nlohmann::json compare_values(const nlohmann::json& left, const std::string& op, const nlohmann::json& right) {
        // If both numbers, compare numerically
        if (left.is_number() && right.is_number()) {
            double a = left.get<double>();
            double b = right.get<double>();
            if (op == "==") return a == b;
            if (op == "!=") return a != b;
            if (op == ">") return a > b;
            if (op == "<") return a < b;
            if (op == ">=") return a >= b;
            if (op == "<=") return a <= b;
        }

        // If both booleans
        if (left.is_boolean() && right.is_boolean()) {
            bool a = left.get<bool>();
            bool b = right.get<bool>();
            if (op == "==") return a == b;
            if (op == "!=") return a != b;
            // other ops not meaningful
            return false;
        }

        // Fallback to string comparison
        std::string a = left.is_string() ? left.get<std::string>() : left.dump();
        std::string b = right.is_string() ? right.get<std::string>() : right.dump();

        if (op == "==") return a == b;
        if (op == "!=") return a != b;
        if (op == ">") return a > b;
        if (op == "<") return a < b;
        if (op == ">=") return a >= b;
        if (op == "<=") return a <= b;
        return false;
    }
};

std::string Blade::render(std::string_view tpl, const nlohmann::json& context) const
{
    std::string result{tpl};
    std::smatch match;

    // 1. @foreach(items as item) ... @endforeach
    std::regex foreach_regex(R"(@foreach\(\s*([a-zA-Z0-9._]+)\s+as\s+([a-zA-Z0-9._]+)\s*\)([\s\S]*?)@endforeach)");
    while (std::regex_search(result, match, foreach_regex)) {
        std::string key = match[1].str();
        std::string var_name = match[2].str();
        std::string inner_content = match[3].str();

        std::string repeated_content;
        if (context.contains(key) && context[key].is_array()) {
            for (const auto& item : context[key]) {
                nlohmann::json loop_data = context;
                loop_data[var_name] = item;
                repeated_content += render(inner_content, loop_data);
            }
        }

        result.replace(match.position(), match.length(), repeated_content);
    }

    // 2. @if(condition) ... @endif
    std::regex if_regex(R"(@if\(\s*([\s\S]*?)\s*\)([\s\S]*?)@endif)");
    while (std::regex_search(result, match, if_regex)) {
        std::string expr = match[1].str();
        std::string inner_content = match[2].str();
        bool condition = false;
        try {
            ExprParser p(expr, context);
            condition = p.parse();
        } catch (...) {
            condition = false;
        }

        result.replace(match.position(), match.length(), condition ? inner_content : "");
    }

    // 3. @unless(condition) ... @endunless
    std::regex unless_regex(R"(@unless\(\s*([\s\S]*?)\s*\)([\s\S]*?)@endunless)");
    while (std::regex_search(result, match, unless_regex)) {
        std::string expr = match[1].str();
        std::string inner_content = match[2].str();
        bool condition = false;
        try {
            ExprParser p(expr, context);
            condition = p.parse();
        } catch (...) {
            condition = false;
        }

        result.replace(match.position(), match.length(), !condition ? inner_content : "");
    }

    // 4. {{ var }}
    std::regex var_regex(R"(\{\{\s*([a-zA-Z0-9._]+)\s*\}\})");
    while (std::regex_search(result, match, var_regex)) {
        std::string key = match[1].str();
        std::string value = resolve_data(key, context);
        result.replace(match.position(), match.length(), value);
    }

    return result;
}

} // namespace breeze::support
