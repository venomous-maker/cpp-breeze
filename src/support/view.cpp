#include <breeze/support/view.hpp>
#include <fstream>
#include <regex>
#include <sstream>

namespace breeze::support {

View::View(const std::filesystem::path& views_path) : views_path_(views_path) {}

std::string View::render(const std::string& template_name, const nlohmann::json& data) {
    std::filesystem::path full_path = views_path_ / (template_name + ".html");
    if (!std::filesystem::exists(full_path)) {
        return "View [" + template_name + "] not found at " + full_path.string();
    }

    std::ifstream file(full_path);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    return parse(content, data);
}

std::string View::parse(std::string content, const nlohmann::json& data) {
    std::string result = content;

    // 1. @foreach(items as item) ... @endforeach
    std::regex foreach_regex(R"(@foreach\(\s*([a-zA-Z0-9._]+)\s+as\s+([a-zA-Z0-9._]+)\s*\)([\s\S]*?)@endforeach)");
    std::smatch match;
    while (std::regex_search(result, match, foreach_regex)) {
        std::string key = match[1].str();
        std::string var_name = match[2].str();
        std::string inner_content = match[3].str();
        
        std::string repeated_content = "";
        if (data.contains(key) && data[key].is_array()) {
            for (const auto& item : data[key]) {
                nlohmann::json loop_data = data;
                loop_data[var_name] = item;
                repeated_content += parse(inner_content, loop_data);
            }
        }
        
        result.replace(match.position(), match.length(), repeated_content);
    }

    // 2. @if(condition) ... @endif
    std::regex if_regex(R"(@if\(\s*([a-zA-Z0-9._]+)\s*\)([\s\S]*?)@endif)");
    while (std::regex_search(result, match, if_regex)) {
        std::string key = match[1].str();
        std::string inner_content = match[2].str();
        bool condition = is_truthy(key, data);
        
        result.replace(match.position(), match.length(), condition ? inner_content : "");
    }

    // 3. @unless(condition) ... @endunless
    std::regex unless_regex(R"(@unless\(\s*([a-zA-Z0-9._]+)\s*\)([\s\S]*?)@endunless)");
    while (std::regex_search(result, match, unless_regex)) {
        std::string key = match[1].str();
        std::string inner_content = match[2].str();
        bool condition = is_truthy(key, data);
        
        result.replace(match.position(), match.length(), !condition ? inner_content : "");
    }

    // 4. {{ var }}
    std::regex var_regex(R"(\{\{\s*([a-zA-Z0-9._]+)\s*\}\})");
    while (std::regex_search(result, match, var_regex)) {
        std::string key = match[1].str();
        std::string value = resolve_data(key, data);
        result.replace(match.position(), match.length(), value);
    }

    return result;
}

std::string View::resolve_data(const std::string& key, const nlohmann::json& data) {
    auto parts = split(key, '.');
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

bool View::is_truthy(const std::string& key, const nlohmann::json& data) {
    auto parts = split(key, '.');
    const nlohmann::json* current = &data;

    for (const auto& part : parts) {
        if (current->contains(part)) {
            current = &((*current)[part]);
        } else {
            return false;
        }
    }

    if (current->is_boolean()) return current->get<bool>();
    if (current->is_null()) return false;
    if (current->is_string()) return !current->get<std::string>().empty();
    if (current->is_number()) return current->get<double>() != 0;
    if (current->is_array() || current->is_object()) return !current->empty();
    
    return true;
}

std::vector<std::string> View::split(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

} // namespace breeze::support
