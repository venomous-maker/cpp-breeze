#pragma once

#include "interfaces.hpp"
#include <memory>
#include <string>

namespace breeze::database {

class MongoDBConnection : public IConnection {
public:
    explicit MongoDBConnection(const DatabaseConfig& config);
    ~MongoDBConnection() override;

    // MongoDB-specific extensions
    virtual bool createCollection(const std::string& name, const std::string& options);
    virtual bool dropCollection(const std::string& name);

    virtual bool createIndex(const std::string& collection, const std::string& field, bool unique);

    // Override base methods with MongoDB-specific implementations
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

    // Document operations (MongoDB-specific)
    virtual std::string insertDocument(const std::string& collection, const std::string& json);
    virtual bool updateDocument(const std::string& collection, const std::string& id, const std::string& json);
    virtual bool deleteDocument(const std::string& collection, const std::string& id);

    // Override Model operations for MongoDB
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

