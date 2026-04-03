#include <breeze/database/connection/factory.hpp>

#include <breeze/database/connection/sqlite_connection.hpp>
#include <breeze/database/connection/mysql_connection.hpp>
#include <breeze/database/connection/postgresql_connection.hpp>
#include <breeze/database/connection/mongodb_connection.hpp>

namespace breeze::database {

std::unique_ptr<IConnection> ConnectionFactory::create(const DatabaseConfig& config)
{
    switch (config.type) {
    case DatabaseConfig::Type::SQLite:
        return createSQLiteConnection(config);
    case DatabaseConfig::Type::MySQL:
        return createMySQLConnection(config);
    case DatabaseConfig::Type::PostgreSQL:
        return createPostgreSQLConnection(config);
    case DatabaseConfig::Type::MongoDB:
        return createMongoDBConnection(config);
    default:
        return nullptr;
    }
}

std::unique_ptr<IConnection> ConnectionFactory::createSQLiteConnection(const DatabaseConfig& config)
{
    return std::make_unique<SQLiteConnection>(config);
}

std::unique_ptr<IConnection> ConnectionFactory::createMySQLConnection(const DatabaseConfig& config)
{
    return std::make_unique<MySQLConnection>(config);
}

std::unique_ptr<IConnection> ConnectionFactory::createPostgreSQLConnection(const DatabaseConfig& config)
{
    return std::make_unique<PostgreSQLConnection>(config);
}

std::unique_ptr<IConnection> ConnectionFactory::createMongoDBConnection(const DatabaseConfig& config)
{
    return std::make_unique<MongoDBConnection>(config);
}

} // namespace breeze::database

