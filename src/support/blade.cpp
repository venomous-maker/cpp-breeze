#include <breeze/support/blade.hpp>

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

// Replace the previous template_cache with a thread-safe LRU cache keyed by file path + mtime
struct CacheKey {
    std::filesystem::path path;
    std::filesystem::file_time_type mtime;
};

static inline bool operator==(const CacheKey& a, const CacheKey& b) {
    return a.path == b.path && a.mtime == b.mtime;
}

struct CacheKeyHash {
    std::size_t operator()(CacheKey const& k) const noexcept {
        auto h1 = std::hash<std::string>{}(k.path.string());
        auto h2 = std::hash<std::uint64_t>{}(static_cast<std::uint64_t>(k.mtime.time_since_epoch().count()));
        return h1 ^ (h2 << 1);
    }
};

// LRU cache entry
struct CacheEntry {
    std::shared_ptr<Node> ast;
    std::chrono::steady_clock::time_point created;
};

static std::mutex cache_mutex;
static std::list<CacheKey> lru_list; // most recently used at front
static std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> file_cache;
static size_t CACHE_MAX_ITEMS = 128;
static std::chrono::seconds CACHE_TTL = std::chrono::seconds(300); // 5 minutes

static void cache_put(const CacheKey& key, std::shared_ptr<Node> ast) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    // evict if needed
    if (file_cache.size() >= CACHE_MAX_ITEMS) {
        // remove least recently used from back
        if (!lru_list.empty()) {
            auto old = lru_list.back();
            file_cache.erase(old);
            lru_list.pop_back();
        }
    }
    // insert at front
    lru_list.push_front(key);
    file_cache[key] = CacheEntry{ast, std::chrono::steady_clock::now()};
}

static std::shared_ptr<Node> cache_get(const CacheKey& key) {
    std::lock_guard<std::mutex> lock(cache_mutex);
    auto it = file_cache.find(key);
    if (it == file_cache.end()) return nullptr;
    // check TTL
    auto now = std::chrono::steady_clock::now();
    if (now - it->second.created > CACHE_TTL) {
        // expired
        file_cache.erase(it);
        // remove from lru_list
        lru_list.remove(key);
        return nullptr;
    }
    // move to front of LRU
    lru_list.remove(key);
    lru_list.push_front(key);
    return it->second.ast;
}

// Updated compile_template that takes file cache key
static std::shared_ptr<Node> compile_template_from_content(const std::string& tpl) {
    // hash content to key the in-memory cache when using direct content (non-file)
    size_t key = std::hash<std::string>{}(tpl);
    // reuse existing simple cache for content-based templates to avoid recompile
    static std::mutex content_cache_mutex;
    static std::unordered_map<size_t, std::shared_ptr<Node>> content_cache;
    std::lock_guard<std::mutex> lock(content_cache_mutex);
    if (auto it = content_cache.find(key); it != content_cache.end()) return it->second;
    auto root = std::make_shared<Node>(); root->type = Node::TEXT; root->children = parse_nodes(tpl, 0);
    content_cache.emplace(key, root);
    return root;
}

// New function to compile from file path using file-based cache
static std::shared_ptr<Node> compile_template_from_file(const std::filesystem::path& path) {
    try {
        auto mtime = std::filesystem::last_write_time(path);
        CacheKey key{path, mtime};
        if (auto ast = cache_get(key); ast) return ast;
        // read file
        std::ifstream file(path);
        if (!file) return nullptr;
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        auto ast = std::make_shared<Node>(); ast->type = Node::TEXT; ast->children = parse_nodes(content, 0);
        cache_put(key, ast);
        return ast;
    } catch (...) {
        return nullptr;
    }
}

// parse helpers: find next token position among {{, @if(, @unless(, @foreach(, @endif, @endunless, @endforeach
static size_t find_next_special(const std::string& s, size_t start) {
    std::vector<size_t> pos;
    auto add = [&](const std::string& t){ size_t p = s.find(t, start); if (p != std::string::npos) pos.push_back(p); };
    add("{{"); add("@if("); add("@unless("); add("@foreach("); add("@endif"); add("@endunless"); add("@endforeach");
    if (pos.empty()) return std::string::npos;
    return *std::min_element(pos.begin(), pos.end());
}

static std::string extract_between(const std::string& s, size_t start, const std::string& open, const std::string& close) {
    size_t p = s.find(open, start);
    if (p == std::string::npos) return {};
    size_t q = s.find(close, p + open.size());
    if (q == std::string::npos) return {};
    return s.substr(p + open.size(), q - (p + open.size()));
}

static std::vector<FilterSpec> parse_filters(const std::string& s) {
    std::vector<FilterSpec> out;
    std::istringstream ss(s);
    std::string token;
    while (std::getline(ss, token, '|')) {
        token = trim(token);
        if (token.empty()) continue;
        // parse name(args?)
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
            // remaining text
            auto n = std::make_shared<Node>(); n->type = Node::TEXT; n->text = s.substr(pos); nodes.push_back(n); break;
        }
        if (next > pos) {
            auto n = std::make_shared<Node>(); n->type = Node::TEXT; n->text = s.substr(pos, next - pos); nodes.push_back(n);
        }
        // check what token starts at next
        if (s.compare(next, 2, "{{") == 0) {
            size_t close = s.find("}}", next+2);
            if (close == std::string::npos) {
                auto n = std::make_shared<Node>(); n->type = Node::TEXT; n->text = s.substr(next); nodes.push_back(n); break;
            }
            std::string inside = trim(s.substr(next+2, close - (next+2)));
            // split filters by first '|'
            size_t p = inside.find('|');
            std::string expr = inside;
            std::string filt_str;
            if (p != std::string::npos) { expr = trim(inside.substr(0,p)); filt_str = inside.substr(p+1); }
            auto n = std::make_shared<Node>(); n->type = Node::VAR; n->expr = expr; n->filters = parse_filters(filt_str); nodes.push_back(n);
            pos = close + 2; continue;
        }
        if (s.compare(next, 4, "@if(") == 0) {
            size_t open_par = next + 4;
            size_t close_par = s.find(')', open_par);
            if (close_par == std::string::npos) { pos = next + 4; continue; }
            std::string cond = trim(s.substr(open_par, close_par - open_par));
            // parse inner until matching @endif, respecting nested @if
            size_t inner_start = close_par + 1;
            std::string inner_end_tag = "@endif";
            auto children = parse_nodes(s, inner_start, inner_end_tag);
            auto n = std::make_shared<Node>(); n->type = Node::IF; n->expr = cond; n->children = std::move(children);
            nodes.push_back(n);
            // move pos to after the consumed end tag - parse_nodes will return positioned after end tag via finding it
            // We need to find corresponding end tag position. We'll search for the matching end here.
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
            size_t scan = inner_start; int depth = 1;
            while (scan < s.size() && depth > 0) {
                size_t a = s.find("@unless(", scan);
                size_t b = s.find("@endunless", scan);
                if (b == std::string::npos) { scan = s.size(); break; }
                if (a != std::string::npos && a < b) { depth++; scan = a + 8; } else { depth--; scan = b + 10; }
            }
            std::string inner = s.substr(inner_start, scan - inner_start - std::string("@endunless").size());
            auto children = parse_nodes(inner, 0);
            auto n = std::make_shared<Node>(); n->type = Node::UNLESS; n->expr = cond; n->children = std::move(children);
            nodes.push_back(n);
            pos = scan; continue;
        }
        if (s.compare(next, 9, "@foreach(") == 0) {
            size_t open_par = next + 9;
            size_t close_par = s.find(')', open_par);
            if (close_par == std::string::npos) { pos = next + 9; continue; }
            std::string inside = trim(s.substr(open_par, close_par - open_par));
            // expected "items as item"
            std::regex rx(R"(([a-zA-Z0-9._]+)\s+as\s+([a-zA-Z0-9._]+))");
            std::smatch m;
            std::string list_name, item_name;
            if (std::regex_search(inside, m, rx)) { list_name = m[1].str(); item_name = m[2].str(); }
            size_t inner_start = close_par + 1;
            size_t scan = inner_start; int depth = 1;
            while (scan < s.size() && depth > 0) {
                size_t a = s.find("@foreach(", scan);
                size_t b = s.find("@endforeach", scan);
                if (b == std::string::npos) { scan = s.size(); break; }
                if (a != std::string::npos && a < b) { depth++; scan = a + 9; } else { depth--; scan = b + 10; }
            }
            std::string inner = s.substr(inner_start, scan - inner_start - std::string("@endforeach").size());
            auto children = parse_nodes(inner, 0);
            auto n = std::make_shared<Node>(); n->type = Node::FOREACH; n->list_name = list_name; n->item_name = item_name; n->children = std::move(children);
            nodes.push_back(n);
            pos = scan; continue;
        }
        if (!end_tag.empty()) {
            // check if end_tag starts at 'next'
            if (s.compare(next, end_tag.size(), end_tag) == 0) {
                // consume end_tag and return
                pos = next + end_tag.size();
                return nodes;
            }
        }
        // if none matched, consume one char to avoid infinite loop
        pos = next + 1;
    }
    return nodes;
}

static std::shared_ptr<Node> compile_template(const std::string& tpl) {
    // forward to content-based compilation/cache
    return compile_template_from_content(tpl);
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

} // namespace breeze::support
