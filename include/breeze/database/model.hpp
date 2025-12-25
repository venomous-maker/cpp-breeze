#pragma once

#include <breeze/database/query.hpp>

#include <string>
#include <unordered_map>
#include <utility>

namespace breeze::database {

class Model {
public:
    void set(std::string key, std::string value) { attributes_[std::move(key)] = std::move(value); }

    const std::string* get(const std::string& key) const
    {
        auto it = attributes_.find(key);
        if (it == attributes_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    static Query query(std::string table)
    {
        Query q;
        q.table(std::move(table));
        return q;
    }

private:
    std::unordered_map<std::string, std::string> attributes_;
};

} // namespace breeze::database
