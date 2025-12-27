#include <breeze/support/blade.hpp>
#include <breeze/core/application.hpp>

#include <regex>
#include <sstream>
#include <cctype>
#include <algorithm>
#include <unordered_map>
#include <memory>
#include <filesystem>
#include <mutex>
#include <list>
#include <chrono>
#include <cmath>
#include <fstream>
#include <openssl/sha.h>

namespace breeze::support {

// --- helpers ---
static std::vector<std::string> split_dot(const std::string& s) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, '.')) tokens.push_back(token);
    return tokens;
}

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b-1]))) --b;
    return s.substr(a, b - a);
}

static std::string html_escape(const std::string& s) {
    std::string out; out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

static std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::toupper(c)); });
    return s;
}

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return s;
}

// Resolve dotted json path, return nullptr if not found
static const nlohmann::json* get_json_ptr(const std::string& key, const nlohmann::json& data) {
    auto parts = split_dot(key);
    const nlohmann::json* current = &data;
    for (const auto& part : parts) {
        if (current->contains(part)) current = &((*current)[part]); else return nullptr;
    }
    return current;
}

static std::string resolve_data_simple(const std::string& key, const nlohmann::json& data) {
    if (auto p = get_json_ptr(key, data); p) {
        if (p->is_string()) return p->get<std::string>();
        if (p->is_number()) return p->dump();
        if (p->is_boolean()) return p->get<bool>() ? "true" : "false";
        return p->dump();
    }
    return std::string{};
}

static bool is_truthy_value(const nlohmann::json& current) {
    if (current.is_boolean()) return current.get<bool>();
    if (current.is_null()) return false;
    if (current.is_string()) return !current.get<std::string>().empty();
    if (current.is_number()) return current.get<double>() != 0;
    if (current.is_array() || current.is_object()) return !current.empty();
    return true;
}

// --- expression parsing with errors ---
struct ExprError : public std::runtime_error {
    std::string expr;
    size_t pos;
    ExprError(std::string m, std::string e, size_t p) : std::runtime_error(m), expr(std::move(e)), pos(p) {}
};

class ExprParser {
public:
    ExprParser(const std::string& s, const nlohmann::json& ctx) : str_(s), pos_(0), ctx_(ctx) {}

    nlohmann::json eval() {
        skip_ws();
        auto v = parse_or();
        skip_ws();
        return v;
    }

    bool eval_bool() {
        return is_truthy_value(eval());
    }

private:
    std::string str_;
    size_t pos_;
    const nlohmann::json& ctx_;

    void skip_ws() { while (pos_ < str_.size() && std::isspace(static_cast<unsigned char>(str_[pos_]))) ++pos_; }
    bool starts_with(const std::string& t) { return str_.substr(pos_, t.size()) == t; }

    nlohmann::json parse_or() {
        auto left = parse_and(); skip_ws();
        while (starts_with("||")) { pos_ += 2; skip_ws(); auto right = parse_and(); left = (is_truthy_value(left) || is_truthy_value(right)); skip_ws(); }
        return left;
    }

    nlohmann::json parse_and() {
        auto left = parse_comparison(); skip_ws();
        while (starts_with("&&")) { pos_ += 2; skip_ws(); auto right = parse_comparison(); left = (is_truthy_value(left) && is_truthy_value(right)); skip_ws(); }
        return left;
    }

    nlohmann::json parse_comparison() {
        auto left = parse_additive(); skip_ws();
        const std::vector<std::string> ops = {"==","!=" , ">=","<=","<",">"};
        for (auto& op : ops) {
            if (starts_with(op)) {
                pos_ += op.size(); skip_ws(); auto right = parse_additive(); return compare_values(left, op, right);
            }
        }
        return left;
    }

    nlohmann::json parse_additive() {
        auto left = parse_multiplicative(); skip_ws();
        while (pos_ < str_.size()) {
            if (starts_with("+")) { ++pos_; skip_ws(); auto right = parse_multiplicative(); left = arithmetic_op(left, right, '+'); skip_ws(); continue; }
            if (starts_with("-")) { ++pos_; skip_ws(); auto right = parse_multiplicative(); left = arithmetic_op(left, right, '-'); skip_ws(); continue; }
            break;
        }
        return left;
    }

    nlohmann::json parse_multiplicative() {
        auto left = parse_unary(); skip_ws();
        while (pos_ < str_.size()) {
            if (starts_with("*")) { ++pos_; skip_ws(); auto right = parse_unary(); left = arithmetic_op(left, right, '*'); skip_ws(); continue; }
            if (starts_with("/")) { ++pos_; skip_ws(); auto right = parse_unary(); left = arithmetic_op(left, right, '/'); skip_ws(); continue; }
            if (starts_with("%")) { ++pos_; skip_ws(); auto right = parse_unary(); left = arithmetic_op(left, right, '%'); skip_ws(); continue; }
            break;
        }
        return left;
    }

    nlohmann::json parse_unary() {
        skip_ws();
        if (starts_with("!")) { ++pos_; skip_ws(); auto v = parse_unary(); return !is_truthy_value(v); }
        if (starts_with("-")) { ++pos_; skip_ws(); auto v = parse_unary(); if (v.is_number()) return -v.get<double>(); throw ExprError("Unary - applied to non-number","",pos_); }
        return parse_primary();
    }

    nlohmann::json parse_primary() {
        skip_ws(); if (pos_ >= str_.size()) return nullptr;
        if (str_[pos_] == '(') { ++pos_; skip_ws(); auto v = parse_or(); skip_ws(); if (pos_ < str_.size() && str_[pos_] == ')') ++pos_; else throw ExprError("Missing closing parenthesis", str_, pos_); return v; }
        if (str_[pos_] == '"' || str_[pos_] == '\'') {
            char quote = str_[pos_++]; std::string out; while (pos_ < str_.size() && str_[pos_] != quote) { if (str_[pos_] == '\\' && pos_ + 1 < str_.size()) { out.push_back(str_[pos_ + 1]); pos_ += 2; continue; } out.push_back(str_[pos_++]); } if (pos_ < str_.size() && str_[pos_] == quote) ++pos_; return out; }
        if (std::isalpha(static_cast<unsigned char>(str_[pos_])) || str_[pos_] == '_') {
            size_t start = pos_; while (pos_ < str_.size() && (std::isalnum(static_cast<unsigned char>(str_[pos_])) || str_[pos_] == '_' || str_[pos_] == '.')) ++pos_;
            std::string tok = str_.substr(start, pos_ - start);
            if (tok == "true") return true; if (tok == "false") return false; if (tok == "null") return nullptr;
            if (auto p = get_json_ptr(tok, ctx_); p) return *p; return nullptr;
        }
        if (std::isdigit(static_cast<unsigned char>(str_[pos_])) || (str_[pos_] == '-' && pos_ + 1 < str_.size() && std::isdigit(static_cast<unsigned char>(str_[pos_ + 1])))) {
            size_t start = pos_; if (str_[pos_] == '-') ++pos_; while (pos_ < str_.size() && std::isdigit(static_cast<unsigned char>(str_[pos_]))) ++pos_; if (pos_ < str_.size() && str_[pos_] == '.') { ++pos_; while (pos_ < str_.size() && std::isdigit(static_cast<unsigned char>(str_[pos_]))) ++pos_; }
            std::string num = str_.substr(start, pos_ - start); try { return std::stod(num); } catch (...) { throw ExprError("Invalid number literal", str_, start); }
        }
        throw ExprError("Unexpected token in expression", str_, pos_);
    }

    nlohmann::json compare_values(const nlohmann::json& left, const std::string& op, const nlohmann::json& right) {
        if (left.is_number() && right.is_number()) {
            double a = left.get<double>(); double b = right.get<double>();
            if (op == "==") return a == b; if (op == "!=") return a != b; if (op == ">") return a > b; if (op == "<") return a < b; if (op == ">=") return a >= b; if (op == "<=") return a <= b;
        }
        if (left.is_boolean() && right.is_boolean()) {
            bool a = left.get<bool>(); bool b = right.get<bool>(); if (op == "==") return a == b; if (op == "!=") return a != b; return false;
        }
        std::string a = left.is_string() ? left.get<std::string>() : left.dump();
        std::string b = right.is_string() ? right.get<std::string>() : right.dump();
        if (op == "==") return a == b; if (op == "!=") return a != b; if (op == ">") return a > b; if (op == "<") return a < b; if (op == ">=") return a >= b; if (op == "<=") return a <= b; return false;
    }

    nlohmann::json arithmetic_op(const nlohmann::json& left, const nlohmann::json& right, char op) {
        if (left.is_number() && right.is_number()) {
            double a = left.get<double>(); double b = right.get<double>();
            if (op == '+') return a + b; if (op == '-') return a - b; if (op == '*') return a * b; if (op == '/') { if (b == 0) throw ExprError("Division by zero", str_, pos_); return a / b; } if (op == '%') { if (b == 0) throw ExprError("Division by zero for modulus", str_, pos_); return std::fmod(a, b); }
        }
        if (op == '+') { std::string a = left.is_string() ? left.get<std::string>() : left.dump(); std::string b = right.is_string() ? right.get<std::string>() : right.dump(); return a + b; }
        throw ExprError("Arithmetic operation on non-numeric operands", str_, pos_);
    }
};

// --- Template AST and compilation caching ---
struct FilterSpec { std::string name; std::string arg; };
struct Node {
    enum Type { TEXT, VAR, IF, UNLESS, FOREACH } type;
    std::string text; // for TEXT
    std::string expr; // for VAR or IF condition
    std::vector<FilterSpec> filters; // for VAR
    std::vector<std::shared_ptr<Node>> children; // for blocks
    // foreach specifics
    std::string list_name;
    std::string item_name;
};

// forward declaration of parse_nodes so compilation unit can reference it earlier
static std::vector<std::shared_ptr<Node>> parse_nodes(const std::string& s, size_t start, const std::string& end_tag="");

// We'll need a fast content hash for cache keys (FNV-1a)
static std::string sha1_hex(const std::string& s) {
    unsigned char digest[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(s.data()), s.size(), digest);
    static const char hex[] = "0123456789abcdef";
    std::string out; out.reserve(SHA_DIGEST_LENGTH*2);
    for (int i = 0; i < SHA_DIGEST_LENGTH; ++i) {
        out.push_back(hex[(digest[i] >> 4) & 0xF]);
        out.push_back(hex[digest[i] & 0xF]);
    }
    return out;
}

// New cache key uses SHA1 string instead of numeric FNV
struct FileCacheKey {
    std::filesystem::path path;
    std::string content_hash; // hex sha1
};
static inline bool operator==(const FileCacheKey& a, const FileCacheKey& b) { return a.path == b.path && a.content_hash == b.content_hash; }
struct FileCacheKeyHash { std::size_t operator()(FileCacheKey const& k) const noexcept { return std::hash<std::string>{}(k.path.string()) ^ (std::hash<std::string>{}(k.content_hash) << 1); } };

// LRU cache structures (thread-safe)
static std::mutex cache_mutex;
static std::list<FileCacheKey> lru_list; // front = most recently used
// Define CacheEntry used by the cache map
struct CacheEntry {
    std::shared_ptr<Node> ast;
    std::chrono::steady_clock::time_point created;
};
static std::unordered_map<FileCacheKey, std::pair<CacheEntry, std::list<FileCacheKey>::iterator>, FileCacheKeyHash> file_content_cache;
static size_t CACHE_MAX_ITEMS = 128;
static std::chrono::seconds CACHE_TTL = std::chrono::seconds(300); // default 5 minutes
static bool INLINE_CPP_ENABLED = false; // default, can be set from app config

// Add basic cache stats
struct CacheStats { size_t hits = 0; size_t misses = 0; size_t entries = 0; };
static CacheStats cache_stats_data;

// helper: ensure cache directory exists
static std::filesystem::path view_cache_dir() {
    std::filesystem::path p = "storage/framework/views";
    std::error_code ec;
    std::filesystem::create_directories(p, ec);
    return p;
}

// Serialize AST (Node) to JSON for disk cache
static nlohmann::json node_to_json(const std::shared_ptr<Node>& node) {
    nlohmann::json j;
    j["type"] = static_cast<int>(node->type);
    j["text"] = node->text;
    j["expr"] = node->expr;
    // filters
    j["filters"] = nlohmann::json::array();
    for (const auto& f : node->filters) {
        nlohmann::json fj;
        fj["name"] = f.name;
        fj["arg"] = f.arg;
        j["filters"].push_back(fj);
    }
    // children
    j["children"] = nlohmann::json::array();
    for (const auto& c : node->children) j["children"].push_back(node_to_json(c));
    j["list_name"] = node->list_name;
    j["item_name"] = node->item_name;
    return j;
}

static std::shared_ptr<Node> json_to_node(const nlohmann::json& j) {
    auto n = std::make_shared<Node>();
    n->type = static_cast<Node::Type>(j.value("type", 0));
    n->text = j.value("text", std::string{});
    n->expr = j.value("expr", std::string{});
    if (j.contains("filters") && j["filters"].is_array()) {
        for (const auto& fj : j["filters"]) {
            FilterSpec f{fj.value("name", std::string{}), fj.value("arg", std::string{})};
            n->filters.push_back(f);
        }
    }
    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& cj : j["children"]) n->children.push_back(json_to_node(cj));
    }
    n->list_name = j.value("list_name", std::string{});
    n->item_name = j.value("item_name", std::string{});
    return n;
}

// LRU cache put/get
static void cache_put_file(const FileCacheKey& key, std::shared_ptr<Node> ast) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = file_content_cache.find(key);
    if (it != file_content_cache.end()) {
        // update entry and move to front
        it->second.first.ast = ast;
        it->second.first.created = std::chrono::steady_clock::now();
        lru_list.erase(it->second.second);
        lru_list.push_front(key);
        it->second.second = lru_list.begin();
    } else {
        // insert
        lru_list.push_front(key);
        CacheEntry entry{ast, std::chrono::steady_clock::now()};
        file_content_cache.emplace(key, std::make_pair(entry, lru_list.begin()));
        // evict if over capacity
        while (file_content_cache.size() > CACHE_MAX_ITEMS) {
            auto last = lru_list.back();
            auto fit = file_content_cache.find(last);
            if (fit != file_content_cache.end()) file_content_cache.erase(fit);
            lru_list.pop_back();
        }
    }
    cache_stats_data.entries = file_content_cache.size();
}

static std::shared_ptr<Node> cache_get_file(const FileCacheKey& key) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = file_content_cache.find(key);
    if (it == file_content_cache.end()) { cache_stats_data.misses++; return nullptr; }
    // check TTL
    auto now = std::chrono::steady_clock::now();
    if (now - it->second.first.created > CACHE_TTL) {
        // expired
        lru_list.erase(it->second.second);
        file_content_cache.erase(it);
        cache_stats_data.entries = file_content_cache.size();
        cache_stats_data.misses++;
        return nullptr;
    }
    // move to front
    lru_list.erase(it->second.second);
    lru_list.push_front(key);
    it->second.second = lru_list.begin();
    cache_stats_data.hits++;
    return it->second.first.ast;
}

// Disk cache helpers: write/read compiled AST JSON
static void write_ast_to_disk(const FileCacheKey& key, const std::shared_ptr<Node>& ast) {
    try {
        auto dir = view_cache_dir();
        std::string fname = key.content_hash + ".json";
        std::filesystem::path p = dir / fname;
        nlohmann::json j = node_to_json(ast);
        std::ofstream ofs(p);
        if (ofs) ofs << j.dump();
    } catch (...) {}
}

static std::shared_ptr<Node> read_ast_from_disk(const FileCacheKey& key) {
    try {
        auto dir = view_cache_dir();
        std::string fname = key.content_hash + ".json";
        std::filesystem::path p = dir / fname;
        if (!std::filesystem::exists(p)) return nullptr;
        std::ifstream ifs(p);
        if (!ifs) return nullptr;
        nlohmann::json j; ifs >> j;
        return json_to_node(j);
    } catch (...) { return nullptr; }
}

// Clear cache API
void Blade::clear_cache() {
    std::lock_guard<std::mutex> lock(cache_mutex);
    file_content_cache.clear(); lru_list.clear(); cache_stats_data = CacheStats{};
    // also clear disk cache files
    try {
        auto dir = view_cache_dir();
        for (auto &entry : std::filesystem::directory_iterator(dir)) std::filesystem::remove(entry.path());
    } catch (...) {}
}

nlohmann::json Blade::cache_stats() {
    std::lock_guard<std::mutex> lock(cache_mutex);
    nlohmann::json out;
    out["hits"] = cache_stats_data.hits;
    out["misses"] = cache_stats_data.misses;
    out["entries"] = file_content_cache.size();
    out["max_items"] = CACHE_MAX_ITEMS;
    out["ttl_seconds"] = CACHE_TTL.count();
    return out;
}

// Helper to read file content
static std::optional<std::string> read_file_to_string(const std::filesystem::path& path) {
    try {
        std::ifstream file(path);
        if (!file) return std::nullopt;
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return content;
    } catch (...) { return std::nullopt; }
}

// Inline C++ handling: detect @cpp{ ... } block
static bool contains_inline_cpp(const std::string& content, std::string& cpp_code) {
    size_t p = content.find("@cpp{");
    if (p == std::string::npos) return false;
    size_t start = p + 5;
    // find matching closing '}' - naive single '}'
    size_t end = content.find('}', start);
    if (end == std::string::npos) return false;
    cpp_code = content.substr(start, end - start);
    return true;
}

// find inline cpp block and return code and position of end
static bool extract_inline_cpp_block(const std::string& content, size_t& open_pos, size_t& close_pos, std::string& cpp_code) {
    open_pos = content.find("@cpp{");
    if (open_pos == std::string::npos) return false;
    size_t start = open_pos + 5;
    // find matching '}' - naive (first '}')
    size_t end = content.find('}', start);
    if (end == std::string::npos) return false;
    cpp_code = content.substr(start, end - start);
    close_pos = end;
    return true;
}

// Try to compile inline cpp code to a temporary executable and run it, capturing stdout.
// Returns optional<string> with output if success, nullopt on failure.
static std::optional<std::string> compile_and_run_cpp(const std::string& code) {
    // create temp source file
    std::string id = std::to_string(std::hash<std::string>{}(code));
    std::string tmp_src = "/tmp/breeze_inline_" + id + ".cpp";
    std::string tmp_bin = "/tmp/breeze_inline_" + id + ".out";
    {
        std::ofstream ofs(tmp_src);
        if (!ofs) return std::nullopt;
        // wrap code in main if it doesn't have main
        if (code.find("int main") == std::string::npos) {
            ofs << "#include <iostream>\nusing namespace std;\nint main(){\n" << code << "\nreturn 0;\n}\n";
        } else {
            ofs << code;
        }
    }
    // compile with g++
    std::string cmd = "g++ -std=c++17 -O2 " + tmp_src + " -o " + tmp_bin + " 2>" + tmp_src + ".err";
    int rc = std::system(cmd.c_str());
    if (rc != 0) {
        return std::nullopt;
    }
    // run and capture output
    std::string run_cmd = tmp_bin + " > " + tmp_bin + ".out 2>&1";
    rc = std::system(run_cmd.c_str());
    if (rc != 0) {
        return std::nullopt;
    }
    // read output file
    std::optional<std::string> out = std::nullopt;
    {
        std::ifstream ifs(tmp_bin + ".out");
        if (ifs) {
            std::string s((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            out = s;
        }
    }
    return out;
}

// Fallback tiny interpreter: supports lines like print("text") or print(var)
static std::string tiny_breeze_interpreter(const std::string& code, const nlohmann::json& ctx) {
    std::istringstream ss(code);
    std::string line;
    std::string output;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.rfind("print(", 0) == 0) {
            size_t a = line.find('(');
            size_t b = line.rfind(')');
            if (a != std::string::npos && b != std::string::npos && b > a) {
                std::string inside = trim(line.substr(a+1, b - (a+1)));
                // if quoted string
                if ((inside.size() >= 2 && ((inside.front() == '"' && inside.back() == '"') || (inside.front() == '\'' && inside.back() == '\'')))) {
                    output += inside.substr(1, inside.size()-2);
                } else {
                    // treat as identifier
                    output += resolve_data_simple(inside, ctx);
                }
                output += '\n';
            }
        }
    }
    return output;
}

// Updated compile_template_from_file to use content-hash key and inline C++ processing with disk cache
static std::shared_ptr<Node> compile_template_from_file(const std::filesystem::path& path) {
    try {
        auto content_opt = read_file_to_string(path);
        if (!content_opt) return nullptr;
        auto content = *content_opt;
        std::string chash = sha1_hex(content);
        FileCacheKey key{path, chash};
        // check in-memory cache
        if (auto ast = cache_get_file(key); ast) return ast;
        // check on-disk cache
        if (auto disk_ast = read_ast_from_disk(key); disk_ast) {
            // If the template contains an inline C++ block and inline compilation is enabled,
            // do not return a cached raw-text AST that simply contains the source (it may be stale);
            // instead continue to attempt inline compilation so the fallback interpreter can be used.
            bool contains_cpp = (content.find("@cpp{") != std::string::npos);
            bool disk_contains_cpp_marker = false;
            if (disk_ast->type == Node::TEXT && disk_ast->text.find("@cpp{") != std::string::npos) disk_contains_cpp_marker = true;
            if (INLINE_CPP_ENABLED && contains_cpp && disk_contains_cpp_marker) {
                // skip using the disk cache so we can attempt compile/fallback
            } else {
                cache_put_file(key, disk_ast);
                return disk_ast;
            }
        }
        // detect inline cpp (only if enabled)
        size_t open_pos=0, close_pos=0;
        std::string cpp_code;
        if (INLINE_CPP_ENABLED && extract_inline_cpp_block(content, open_pos, close_pos, cpp_code)) {
            if (auto out = compile_and_run_cpp(cpp_code); out) {
                auto root = std::make_shared<Node>(); root->type = Node::TEXT; root->text = *out; cache_put_file(key, root); write_ast_to_disk(key, root); return root;
            } else {
                // compilation failed: run tiny interpreter on the suffix after the closing '}' to produce fallback output
                std::string suffix = content.substr(close_pos + 1);
                std::string fallback = tiny_breeze_interpreter(suffix, nlohmann::json::object());
                auto root = std::make_shared<Node>(); root->type = Node::TEXT; root->text = fallback; cache_put_file(key, root); write_ast_to_disk(key, root); return root;
            }
        }
        // parse and cache AST
        auto ast = std::make_shared<Node>(); ast->type = Node::TEXT; ast->children = parse_nodes(content, 0);
        cache_put_file(key, ast);
        write_ast_to_disk(key, ast);
        return ast;
    } catch (...) {
        return nullptr;
    }
}

// Updated compile_template that takes file content and caches by content hash
static std::shared_ptr<Node> compile_template_from_content(const std::string& tpl) {
    // small in-memory content cache keyed by hash
    static std::mutex content_cache_mutex;
    static std::unordered_map<std::size_t, std::shared_ptr<Node>> content_cache;
    std::size_t h = std::hash<std::string>{}(tpl);
    {
        std::lock_guard<std::mutex> lock(content_cache_mutex);
        auto it = content_cache.find(h);
        if (it != content_cache.end()) return it->second;
    }
    auto root = std::make_shared<Node>(); root->type = Node::TEXT; root->children = parse_nodes(tpl, 0);
    {
        std::lock_guard<std::mutex> lock(content_cache_mutex);
        content_cache.emplace(h, root);
    }
    return root;
}

// --- rendering ---
static std::string apply_filters(const std::string& input, const std::vector<FilterSpec>& filters, const nlohmann::json& ctx) {
    std::string out = input;
    for (const auto& f : filters) {
        if (f.name == "escape") out = html_escape(out);
        else if (f.name == "upper") out = to_upper(out);
        else if (f.name == "lower") out = to_lower(out);
        else if (f.name == "trim") out = trim(out);
        else if (f.name == "truncate") {
            // arg is length or expression
            int len = 0;
            if (!f.arg.empty()) {
                try { ExprParser p(f.arg, ctx); auto v = p.eval(); if (v.is_number()) len = static_cast<int>(v.get<double>()); else len = std::stoi(f.arg); } catch (...) { len = 0; }
            }
            if (len > 0 && (int)out.size() > len) out = out.substr(0, len);
        }
        else if (f.name == "default") {
            // arg is expression or literal
            if (out.empty()) {
                if (!f.arg.empty()) {
                    try { ExprParser p(f.arg, ctx); auto v = p.eval(); if (v.is_string()) out = v.get<std::string>(); else if (v.is_number()) out = v.dump(); else if (v.is_boolean()) out = v.get<bool>() ? "true" : "false"; else out = ""; }
                    catch (...) { out = f.arg; }
                }
            }
        }
        else if (f.name == "format") {
            // simple replacement: replace first '{}' with out, or use {0}
            std::string fmt = f.arg;
            if (fmt.empty()) continue;
            size_t p = fmt.find("{}");
            if (p != std::string::npos) {
                std::string res = fmt.substr(0,p) + out + fmt.substr(p+2);
                out = res;
            } else {
                // replace {0}
                size_t q = fmt.find("{0}");
                if (q != std::string::npos) {
                    std::string res = fmt.substr(0,q) + out + fmt.substr(q+3);
                    out = res;
                }
            }
        }
        else {
            // unknown filter: ignore
        }
    }
    return out;
}

static std::string render_nodes(const std::vector<std::shared_ptr<Node>>& nodes, const nlohmann::json& ctx);

static std::string render_node(const std::shared_ptr<Node>& node, const nlohmann::json& ctx) {
    try {
        switch (node->type) {
            case Node::TEXT: return node->text;
            case Node::VAR: {
                ExprParser p(node->expr, ctx);
                nlohmann::json v = p.eval();
                std::string out;
                if (v.is_string()) out = v.get<std::string>();
                else if (v.is_number()) out = v.dump();
                else if (v.is_boolean()) out = v.get<bool>() ? "true" : "false";
                else if (v.is_null()) out = ""; else out = v.dump();
                out = apply_filters(out, node->filters, ctx);
                return out;
            }
            case Node::IF: {
                ExprParser p(node->expr, ctx);
                bool cond = p.eval_bool();
                if (cond) return render_nodes(node->children, ctx);
                return std::string{};
            }
            case Node::UNLESS: {
                ExprParser p(node->expr, ctx);
                bool cond = p.eval_bool();
                if (!cond) return render_nodes(node->children, ctx);
                return std::string{};
            }
            case Node::FOREACH: {
                // lookup list
                if (auto p = get_json_ptr(node->list_name, ctx); p && p->is_array()) {
                    std::string out;
                    for (const auto& item : *p) {
                        nlohmann::json local = ctx;
                        local[node->item_name] = item;
                        out += render_nodes(node->children, local);
                    }
                    return out;
                }
                return std::string{};
            }
        }
    } catch (const ExprError& e) {
        std::ostringstream oss; oss << "[Template Error: expr=\"" << e.expr << "\" pos=" << e.pos << " msg=" << e.what() << "]"; return oss.str();
    } catch (const std::exception& ex) {
        std::ostringstream oss; oss << "[Template Error: msg=" << ex.what() << "]"; return oss.str();
    }
    return std::string{};
}

static std::string render_nodes(const std::vector<std::shared_ptr<Node>>& nodes, const nlohmann::json& ctx) {
    std::string out;
    for (const auto& n : nodes) out += render_node(n, ctx);
    return out;
}

// Update Blade::render to use compile_template_from_content
std::string Blade::render(std::string_view tpl, const nlohmann::json& context) const {
    std::string content{tpl};
    auto root = compile_template_from_content(content);
    try {
        return render_nodes(root->children, context);
    } catch (const ExprError& e) {
        std::ostringstream oss; oss << "[Template Error: expr=\"" << e.expr << "\" pos=" << e.pos << " msg=" << e.what() << "]"; return oss.str();
    } catch (const std::exception& ex) {
        std::ostringstream oss; oss << "[Template Error: msg=" << ex.what() << "]"; return oss.str();
    }
}

// Implement render_from_file using file-based cache
std::string Blade::render_from_file(const std::filesystem::path& file_path, const nlohmann::json& context) const {
    auto ast = compile_template_from_file(file_path);
    if (!ast) return "View not found: " + file_path.string();
    try {
        return render_nodes(ast->children, context);
    } catch (const ExprError& e) {
        std::ostringstream oss; oss << "[Template Error: expr=\"" << e.expr << "\" pos=" << e.pos << " msg=" << e.what() << "]"; return oss.str();
    } catch (const std::exception& ex) {
        std::ostringstream oss; oss << "[Template Error: msg=" << ex.what() << "]"; return oss.str();
    }
}

// Adjust cache parameters and inline flag from application config at runtime (called when rendering from file)
static void load_cache_config_from_app() {
    try {
        if (breeze::core::Application::has_instance()) {
            auto& app = breeze::core::Application::instance();
            CACHE_MAX_ITEMS = std::stoul(app.config().get("view.cache.max_items", std::to_string(CACHE_MAX_ITEMS)));
            int ttl = std::stoi(app.config().get("view.cache.ttl_seconds", std::to_string(CACHE_TTL.count())));
            CACHE_TTL = std::chrono::seconds(ttl);
            std::string inline_flag = app.config().get("view.inline_cpp.enabled", "false");
            std::transform(inline_flag.begin(), inline_flag.end(), inline_flag.begin(), [](unsigned char c){ return std::tolower(c); });
            INLINE_CPP_ENABLED = (inline_flag == "1" || inline_flag == "true" || inline_flag == "yes");
        }
    } catch (...) {
        // ignore
    }
    // env var override
    const char* env = std::getenv("BREEZE_INLINE_CPP");
    if (env) {
        std::string e(env);
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c){ return std::tolower(c); });
        INLINE_CPP_ENABLED = (e == "1" || e == "true" || e == "yes");
    }
    if (INLINE_CPP_ENABLED) std::cerr << "[Warning] Inline C++ compilation is ENABLED (BREEZE_INLINE_CPP or view.inline_cpp.enabled). Only enable for trusted templates." << std::endl;
}

// Ensure cache config is loaded before first render_from_file
static struct CacheConfigLoader { CacheConfigLoader() { load_cache_config_from_app(); } } cache_config_loader_instance;

// parse helpers: find next token position among {{, @if(, @unless(, @foreach(, @endif, @endunless, @endforeach
static size_t find_next_special(const std::string& s, size_t start) {
    std::vector<size_t> pos;
    auto add = [&](const std::string& t){ size_t p = s.find(t, start); if (p != std::string::npos) pos.push_back(p); };
    add("{{"); add("@if("); add("@unless("); add("@foreach("); add("@endif"); add("@endunless"); add("@endforeach");
    if (pos.empty()) return std::string::npos;
    return *std::min_element(pos.begin(), pos.end());
}

static std::vector<FilterSpec> parse_filters(const std::string& s) {
    std::vector<FilterSpec> out;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, '|')) {
        token = trim(token);
        if (token.empty()) continue;
        size_t p = token.find('(');
        if (p == std::string::npos) { out.push_back({token, ""}); continue; }
        size_t q = token.rfind(')');
        std::string name = trim(token.substr(0, p));
        std::string arg = "";
        if (q != std::string::npos && q > p) arg = trim(token.substr(p+1, q - (p+1)));
        out.push_back({name, arg});
    }
    return out;
}

static std::vector<std::shared_ptr<Node>> parse_nodes(const std::string& s, size_t start, const std::string& end_tag) {
    std::vector<std::shared_ptr<Node>> nodes;
    size_t pos = start;
    while (pos < s.size()) {
        size_t next = find_next_special(s, pos);
        if (next == std::string::npos) {
            auto n = std::make_shared<Node>(); n->type = Node::TEXT; n->text = s.substr(pos); nodes.push_back(n); break;
        }
        if (next > pos) {
            auto n = std::make_shared<Node>(); n->type = Node::TEXT; n->text = s.substr(pos, next - pos); nodes.push_back(n);
        }
        if (!end_tag.empty() && s.compare(next, end_tag.size(), end_tag) == 0) {
            // consume end_tag and return
            pos = next + end_tag.size();
            return nodes;
        }
        if (s.compare(next, 2, "{{") == 0) {
            size_t close = s.find("}}", next+2);
            if (close == std::string::npos) { auto n = std::make_shared<Node>(); n->type = Node::TEXT; n->text = s.substr(next); nodes.push_back(n); break; }
            std::string inside = trim(s.substr(next+2, close - (next+2)));
            size_t p = inside.find('|');
            std::string expr = inside; std::string filt_str;
            if (p != std::string::npos) { expr = trim(inside.substr(0,p)); filt_str = inside.substr(p+1); }
            auto n = std::make_shared<Node>(); n->type = Node::VAR; n->expr = expr; n->filters = parse_filters(filt_str); nodes.push_back(n);
            pos = close + 2; continue;
        }
        if (s.compare(next, 4, "@if(") == 0) {
            size_t open_par = next + 4;
            size_t close_par = s.find(')', open_par);
            if (close_par == std::string::npos) { pos = next + 4; continue; }
            std::string cond = trim(s.substr(open_par, close_par - open_par));
            size_t inner_start = close_par + 1;
            auto children = parse_nodes(s, inner_start, "@endif");
            auto n = std::make_shared<Node>(); n->type = Node::IF; n->expr = cond; n->children = std::move(children);
            nodes.push_back(n);
            // advance pos beyond matching @endif
            size_t scan = inner_start; int depth = 1;
            while (scan < s.size() && depth > 0) {
                size_t a = s.find("@if(", scan);
                size_t b = s.find("@endif", scan);
                if (b == std::string::npos) { scan = s.size(); break; }
                if (a != std::string::npos && a < b) { depth++; scan = a + 4; } else { depth--; scan = b + 6; }
            }
            pos = scan; continue;
        }
        if (s.compare(next, 8, "@unless(") == 0) {
            size_t open_par = next + 8;
            size_t close_par = s.find(')', open_par);
            if (close_par == std::string::npos) { pos = next + 8; continue; }
            std::string cond = trim(s.substr(open_par, close_par - open_par));
            size_t inner_start = close_par + 1;
            auto children = parse_nodes(s, inner_start, "@endunless");
            auto n = std::make_shared<Node>(); n->type = Node::UNLESS; n->expr = cond; n->children = std::move(children);
            nodes.push_back(n);
            size_t scan = inner_start; int depth = 1;
            while (scan < s.size() && depth > 0) {
                size_t a = s.find("@unless(", scan);
                size_t b = s.find("@endunless", scan);
                if (b == std::string::npos) { scan = s.size(); break; }
                if (a != std::string::npos && a < b) { depth++; scan = a + 8; } else { depth--; scan = b + 10; }
            }
            pos = scan; continue;
        }
        if (s.compare(next, 9, "@foreach(") == 0) {
            size_t open_par = next + 9;
            size_t close_par = s.find(')', open_par);
            if (close_par == std::string::npos) { pos = next + 9; continue; }
            std::string inside = trim(s.substr(open_par, close_par - open_par));
            std::regex rx(R"(([a-zA-Z0-9._]+)\s+as\s+([a-zA-Z0-9._]+))");
            std::smatch m;
            std::string list_name, item_name;
            if (std::regex_search(inside, m, rx)) { list_name = m[1].str(); item_name = m[2].str(); }
            size_t inner_start = close_par + 1;
            auto children = parse_nodes(s, inner_start, "@endforeach");
            auto n = std::make_shared<Node>(); n->type = Node::FOREACH; n->list_name = list_name; n->item_name = item_name; n->children = std::move(children);
            nodes.push_back(n);
            size_t scan = inner_start; int depth = 1;
            while (scan < s.size() && depth > 0) {
                size_t a = s.find("@foreach(", scan);
                size_t b = s.find("@endforeach", scan);
                if (b == std::string::npos) { scan = s.size(); break; }
                if (a != std::string::npos && a < b) { depth++; scan = a + 9; } else { depth--; scan = b + 10; }
            }
            pos = scan; continue;
        }
        // fallback to move one char to avoid infinite loop
        pos = next + 1;
    }
    return nodes;
}

} // namespace breeze::support
