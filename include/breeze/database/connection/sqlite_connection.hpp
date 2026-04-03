#pragma once

#include "interfaces.hpp"
#include <memory>
#include <string>
#include <vector>

namespace breeze::database {

class SQLiteConnection : public IConnection {
public:
    explicit SQLiteConnection(const DatabaseConfig& config);
    ~SQLiteConnection() override;

    // Implement all IConnection methods in source

    bool connect() override;
    void disconnect() override;
    bool isConnected() const override;
    bool ping() override;

    std::unique_ptr<IResultSet> executeQuery(const std::string& query) override;
    int executeUpdate(const std::string& query) override;
    int execute(const std::string& query) override;

    std::unique_ptr<IResultSet> executePrepared(
        const std::string& query,
        const std::vector<Value>& params) override;

    int executePreparedUpdate(
        const std::string& query,
        const std::vector<Value>& params) override;

    bool beginTransaction() override;
    bool commit() override;
    bool rollback() override;

    bool createDatabase(const std::string& name) override;
    bool dropDatabase(const std::string& name) override;
    bool useDatabase(const std::string& name) override;

    bool createTable(const std::string& name, const std::string& schema) override;
    bool dropTable(const std::string& name) override;
    bool truncateTable(const std::string& name) override;

    std::string escapeString(const std::string& str) override;
    std::string getLastError() const override;
    int64_t getLastInsertId() const override;
    int getAffectedRows() const override;

    std::vector<Model> find(const Query& query) override;
    bool insert(const std::string& table, const Model& model) override;
    bool update(const std::string& table, const Model& model, const Query& where) override;
    bool remove(const std::string& table, const Query& where) override;

    void executeMigration(const std::string& sql) override;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace breeze::database

