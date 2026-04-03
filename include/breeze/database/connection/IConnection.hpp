#pragma once

#include "types.hpp"
#include <memory>
#include <string>
#include <vector>

#include <breeze/database/query.hpp>
#include <breeze/database/model.hpp>

namespace breeze::database {

class IResultSet {
public:
    virtual ~IResultSet() = default;

    virtual bool next() = 0;
    virtual int columnCount() const = 0;
    virtual std::string columnName(int index) const = 0;

    virtual Value get(int index) const = 0;
    virtual Value get(const std::string& column) const = 0;

    template<typename T>
    T getAs(int index) const {
        return std::get<T>(get(index));
    }

    template<typename T>
    T getAs(const std::string& column) const {
        return std::get<T>(get(column));
    }
};

class IConnection {
public:
    virtual ~IConnection() = default;

    // Connection management
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool isConnected() const = 0;
    virtual bool ping() = 0;

    // Query execution
    virtual std::unique_ptr<IResultSet> executeQuery(const std::string& query) = 0;
    virtual int executeUpdate(const std::string& query) = 0;
    virtual int execute(const std::string& query) = 0;

    // Prepared statements
    virtual std::unique_ptr<IResultSet> executePrepared(
        const std::string& query,
        const std::vector<Value>& params) = 0;

    virtual int executePreparedUpdate(
        const std::string& query,
        const std::vector<Value>& params) = 0;

    // Transaction management
    virtual bool beginTransaction() = 0;
    virtual bool commit() = 0;
    virtual bool rollback() = 0;

    // Database operations
    virtual bool createDatabase(const std::string& name) = 0;
    virtual bool dropDatabase(const std::string& name) = 0;
    virtual bool useDatabase(const std::string& name) = 0;

    // Table operations
    virtual bool createTable(const std::string& name, const std::string& schema) = 0;
    virtual bool dropTable(const std::string& name) = 0;
    virtual bool truncateTable(const std::string& name) = 0;

    // Utility methods
    virtual std::string escapeString(const std::string& str) = 0;
    virtual std::string getLastError() const = 0;
    virtual int64_t getLastInsertId() const = 0;
    virtual int getAffectedRows() const = 0;

    // Model support
    virtual std::vector<Model> find(const Query& query) = 0;
    virtual bool insert(const std::string& table, const Model& model) = 0;
    virtual bool update(const std::string& table, const Model& model, const Query& where) = 0;
    virtual bool remove(const std::string& table, const Query& where) = 0;

    // Migration support
    virtual void executeMigration(const std::string& sql) = 0;
};

} // namespace breeze::database

