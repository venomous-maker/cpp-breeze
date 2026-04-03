#pragma once

#include "interfaces.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace breeze::database {

class DatabaseManager {
public:
    DatabaseManager() = default;

    void addConnection(const std::string& name, std::unique_ptr<IConnection> connection) {
        connections_[name] = std::move(connection);
    }

    IConnection* getConnection(const std::string& name = "default") {
        auto it = connections_.find(name);
        return it != connections_.end() ? it->second.get() : nullptr;
    }

    bool removeConnection(const std::string& name) {
        return connections_.erase(name) > 0;
    }

    std::vector<std::string> getConnectionNames() const {
        std::vector<std::string> names;
        for (const auto& [name, _] : connections_) {
            names.push_back(name);
        }
        return names;
    }

private:
    std::unordered_map<std::string, std::unique_ptr<IConnection>> connections_;
};

} // namespace breeze::database

