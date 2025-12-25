#pragma once

#include <any>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>

namespace breeze::core {

class Container {
public:
    template <class T>
    void set(std::string key, T value)
    {
        services_[std::move(key)] = std::make_any<T>(std::move(value));
    }

    template <class T>
    T& get(const std::string& key)
    {
        auto it = services_.find(key);
        if (it == services_.end()) {
            throw std::out_of_range("service not found: " + key);
        }
        try {
            return std::any_cast<T&>(it->second);
        } catch (const std::bad_any_cast&) {
            throw std::runtime_error("bad service type for key: " + key);
        }
    }

    bool has(const std::string& key) const { return services_.contains(key); }

private:
    std::unordered_map<std::string, std::any> services_;
};

} // namespace breeze::core
