#include <breeze/support/json_dot.hpp>
#include <cassert>
#include <iostream>

int main() {
    using namespace breeze::support;
    nlohmann::json data = R"({"a": {"b": [{"name": "x"}, {"name": "y"}]}, "x": 5, "key.with.dot": {"inner": 1}})"_json;

    nlohmann::json out;
    bool ok = json_get_dot(data, "a.b.1.name", out);
    assert(ok);
    assert(out.is_string() && out.get<std::string>() == "y");

    auto v = json_get_dot_or(data, "a.b.0.name", "");
    assert(v.is_string() && v.get<std::string>() == "x");

    auto missing = json_get_dot_or(data, "a.b.2.name", "missing");
    assert(missing.is_string() && missing.get<std::string>() == "missing");

    auto root = json_get_dot_or(data, "", nlohmann::json{});
    assert(root.is_object());

    // type mismatch: try numeric index on object
    bool bad = json_get_dot(data, "a.0", out);
    assert(!bad);

    // escaped dot in key: access "key.with.dot" via "key\.with\.dot.inner"
    bool esc = json_get_dot(data, "key\\.with\\.dot.inner", out);
    assert(esc);
    assert(out.is_number_integer() && out.get<int>() == 1);

    std::cout << "json_dot_test: OK\n";
    return 0;
}
