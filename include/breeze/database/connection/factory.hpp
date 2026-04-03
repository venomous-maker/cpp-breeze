#pragma once

#include "types.hpp"
#include "interfaces.hpp"
#include <memory>

namespace breeze::database {

class ConnectionFactory {
public:
    static std::unique_ptr<IConnection> create(const DatabaseConfig& config);

private:
    static std::unique_ptr<IConnection> createSQLiteConnection(const DatabaseConfig& config);
    static std::unique_ptr<IConnection> createMySQLConnection(const DatabaseConfig& config);
    static std::unique_ptr<IConnection> createPostgreSQLConnection(const DatabaseConfig& config);
    static std::unique_ptr<IConnection> createMongoDBConnection(const DatabaseConfig& config);
};

} // namespace breeze::database

