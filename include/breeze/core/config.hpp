#pragma once

#include <string>
#include <unordered_map>
#include <utility>

namespace breeze::core {

class Config {
public:
    void set(std::string key, std::string value) { values_[std::move(key)] = std::move(value); }

    std::string get(const std::string& key, std::string fallback = {}) const
    {
        auto it = values_.find(key);
        if (it == values_.end()) {
            return fallback;
        }
        return it->second;
    }

    bool has(const std::string& key) const { return values_.contains(key); }

private:
    std::unordered_map<std::string, std::string> values_;
};

} // namespace breeze::core
