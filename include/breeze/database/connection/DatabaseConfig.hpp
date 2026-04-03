#pragma once

#include <variant>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace breeze::database {

// Type aliases for database values
using Value = std::variant<
    std::nullptr_t,
    int,
    int64_t,
    double,
    bool,
    std::string,
    std::vector<char>
>;

// Database configuration
struct DatabaseConfig {
    enum class Type {
        SQLite,
        MySQL,
        PostgreSQL,
        MongoDB
    };

    Type type;
    std::string host;
    std::string port;
    std::string database;
    std::string username;
    std::string password;
    std::unordered_map<std::string, std::string> options;
};

} // namespace breeze::database

