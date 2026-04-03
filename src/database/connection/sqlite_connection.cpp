#include <breeze/database/connection/sqlite_connection.hpp>

#ifdef __has_include
# if __has_include(<sqlite3.h>)
#  define BREEZE_HAVE_SQLITE 1
#  include <sqlite3.h>
# endif
#endif

#include <sstream>
#include <algorithm>
#include <cstring>
#include <breeze/database/connection/utils.hpp>

namespace breeze::database {

class SQLiteConnection::Impl {
public:
#ifdef BREEZE_HAVE_SQLITE
    Impl() : handle(nullptr), last_error(), connected(false), last_insert_id(0), affected_rows(0) {}
    sqlite3* handle;
#else
    Impl() : handle(nullptr), last_error(), connected(false), last_insert_id(0), affected_rows(0) {}
    void* handle;
#endif
    DatabaseConfig cfg;
    std::string last_error;
    bool connected;
    int64_t last_insert_id;
    int affected_rows;
};

#ifdef BREEZE_HAVE_SQLITE
class SQLiteResultSet : public IResultSet {
public:
    explicit SQLiteResultSet(sqlite3_stmt* stmt)
        : stmt_(stmt), has_row_(false) {}

    ~SQLiteResultSet() override {
        if (stmt_) sqlite3_finalize(stmt_);
    }

    bool next() override {
        if (!stmt_) return false;
        int rc = sqlite3_step(stmt_);
        has_row_ = (rc == SQLITE_ROW);
        return has_row_;
    }

    int columnCount() const override {
        if (!stmt_) return 0;
        return sqlite3_column_count(stmt_);
    }

    std::string columnName(int index) const override {
        if (!stmt_) return std::string();
        const char* name = sqlite3_column_name(stmt_, index);
        return name ? std::string(name) : std::string();
    }

    Value get(int index) const override {
        if (!stmt_ || !has_row_) return nullptr;
        if (index < 0 || index >= columnCount()) return nullptr;

        int type = sqlite3_column_type(stmt_, index);
        switch (type) {
            case SQLITE_INTEGER:
                return static_cast<int64_t>(sqlite3_column_int64(stmt_, index));
            case SQLITE_FLOAT:
                return sqlite3_column_double(stmt_, index);
            case SQLITE_BLOB: {
                const void* blob = sqlite3_column_blob(stmt_, index);
                int len = sqlite3_column_bytes(stmt_, index);
                if (blob && len > 0) {
                    const char* ptr = static_cast<const char*>(blob);
                    return std::vector<char>(ptr, ptr + len);
                }
                return std::vector<char>();
            }
            case SQLITE_NULL:
                return nullptr;
            case SQLITE_TEXT:
            default: {
                const unsigned char* text = sqlite3_column_text(stmt_, index);
                int len = sqlite3_column_bytes(stmt_, index);
                if (text) {
                    return std::string(reinterpret_cast<const char*>(text), len);
                }
                return std::string();
            }
        }
    }

    Value get(const std::string& column) const override {
        if (!stmt_ || !has_row_) return nullptr;
        int cols = columnCount();
        for (int i = 0; i < cols; ++i) {
            const char* name = sqlite3_column_name(stmt_, i);
            if (name && column == name) return get(i);
        }
        return nullptr;
    }

private:
    sqlite3_stmt* stmt_;
    bool has_row_;
};
#endif

SQLiteConnection::SQLiteConnection(const DatabaseConfig& config)
    : impl_(std::make_unique<Impl>())
{
    impl_->cfg = config;
}

SQLiteConnection::~SQLiteConnection() {
    disconnect();
}

bool SQLiteConnection::connect()
{
#ifdef BREEZE_HAVE_SQLITE
    if (impl_->connected && impl_->handle) return true;

    std::string dbPath = impl_->cfg.database;
    if (dbPath.empty()) {
        dbPath = ":memory:";
    }

    int rc = sqlite3_open(dbPath.c_str(), &impl_->handle);
    if (rc != SQLITE_OK) {
        impl_->last_error = impl_->handle ? sqlite3_errmsg(impl_->handle) : "sqlite3_open failed";
        if (impl_->handle) {
            sqlite3_close(impl_->handle);
            impl_->handle = nullptr;
        }
        impl_->connected = false;
        return false;
    }

    // Enable foreign keys
    sqlite3_exec(impl_->handle, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);

    impl_->connected = true;
    return true;
#else
    impl_->connected = true;
    return true;
#endif
}

void SQLiteConnection::disconnect()
{
#ifdef BREEZE_HAVE_SQLITE
    if (impl_->handle) {
        sqlite3_close(impl_->handle);
        impl_->handle = nullptr;
    }
    impl_->connected = false;
#else
    impl_->connected = false;
#endif
}

bool SQLiteConnection::isConnected() const
{
    return impl_->connected;
}

bool SQLiteConnection::ping()
{
#ifdef BREEZE_HAVE_SQLITE
    if (!impl_->handle) return false;
    // SQLite doesn't have ping, but we can run a simple query
    char* errmsg = nullptr;
    int rc = sqlite3_exec(impl_->handle, "SELECT 1", nullptr, nullptr, &errmsg);
    if (errmsg) sqlite3_free(errmsg);
    return rc == SQLITE_OK;
#else
    return impl_->connected;
#endif
}

std::unique_ptr<IResultSet> SQLiteConnection::executeQuery(const std::string& query)
{
#ifdef BREEZE_HAVE_SQLITE
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return nullptr;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->handle, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        impl_->last_error = sqlite3_errmsg(impl_->handle);
        return nullptr;
    }

    return std::make_unique<SQLiteResultSet>(stmt);
#else
    (void)query;
    return nullptr;
#endif
}

int SQLiteConnection::executeUpdate(const std::string& query)
{
#ifdef BREEZE_HAVE_SQLITE
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return 0;
    }

    char* errmsg = nullptr;
    int rc = sqlite3_exec(impl_->handle, query.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        impl_->last_error = errmsg ? errmsg : "Unknown error";
        if (errmsg) sqlite3_free(errmsg);
        return 0;
    }

    impl_->affected_rows = sqlite3_changes(impl_->handle);
    impl_->last_insert_id = sqlite3_last_insert_rowid(impl_->handle);
    return impl_->affected_rows;
#else
    (void)query;
    return 0;
#endif
}

int SQLiteConnection::execute(const std::string& query)
{
    return executeUpdate(query);
}

std::unique_ptr<IResultSet> SQLiteConnection::executePrepared(
    const std::string& query, const std::vector<Value>& params)
{
#ifdef BREEZE_HAVE_SQLITE
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return nullptr;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->handle, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        impl_->last_error = sqlite3_errmsg(impl_->handle);
        return nullptr;
    }

    // Bind parameters
    for (size_t i = 0; i < params.size(); ++i) {
        int idx = static_cast<int>(i + 1); // SQLite indices are 1-based
        std::visit([&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                sqlite3_bind_null(stmt, idx);
            } else if constexpr (std::is_same_v<T, int>) {
                sqlite3_bind_int(stmt, idx, v);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                sqlite3_bind_int64(stmt, idx, v);
            } else if constexpr (std::is_same_v<T, double>) {
                sqlite3_bind_double(stmt, idx, v);
            } else if constexpr (std::is_same_v<T, bool>) {
                sqlite3_bind_int(stmt, idx, v ? 1 : 0);
            } else if constexpr (std::is_same_v<T, std::string>) {
                sqlite3_bind_text(stmt, idx, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
            } else if constexpr (std::is_same_v<T, std::vector<char>>) {
                sqlite3_bind_blob(stmt, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
            }
        }, params[i]);
    }

    return std::make_unique<SQLiteResultSet>(stmt);
#else
    (void)query; (void)params;
    return nullptr;
#endif
}

int SQLiteConnection::executePreparedUpdate(
    const std::string& query, const std::vector<Value>& params)
{
#ifdef BREEZE_HAVE_SQLITE
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->handle, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        impl_->last_error = sqlite3_errmsg(impl_->handle);
        return 0;
    }

    // Bind parameters
    for (size_t i = 0; i < params.size(); ++i) {
        int idx = static_cast<int>(i + 1);
        std::visit([&](auto&& v) {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                sqlite3_bind_null(stmt, idx);
            } else if constexpr (std::is_same_v<T, int>) {
                sqlite3_bind_int(stmt, idx, v);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                sqlite3_bind_int64(stmt, idx, v);
            } else if constexpr (std::is_same_v<T, double>) {
                sqlite3_bind_double(stmt, idx, v);
            } else if constexpr (std::is_same_v<T, bool>) {
                sqlite3_bind_int(stmt, idx, v ? 1 : 0);
            } else if constexpr (std::is_same_v<T, std::string>) {
                sqlite3_bind_text(stmt, idx, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
            } else if constexpr (std::is_same_v<T, std::vector<char>>) {
                sqlite3_bind_blob(stmt, idx, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
            }
        }, params[i]);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        impl_->last_error = sqlite3_errmsg(impl_->handle);
        return 0;
    }

    impl_->affected_rows = sqlite3_changes(impl_->handle);
    impl_->last_insert_id = sqlite3_last_insert_rowid(impl_->handle);
    return impl_->affected_rows;
#else
    (void)query; (void)params;
    return 0;
#endif
}

bool SQLiteConnection::beginTransaction()
{
#ifdef BREEZE_HAVE_SQLITE
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return false;
    }
    char* errmsg = nullptr;
    int rc = sqlite3_exec(impl_->handle, "BEGIN TRANSACTION", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        impl_->last_error = errmsg ? errmsg : "BEGIN failed";
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }
    return true;
#else
    return true;
#endif
}

bool SQLiteConnection::commit()
{
#ifdef BREEZE_HAVE_SQLITE
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return false;
    }
    char* errmsg = nullptr;
    int rc = sqlite3_exec(impl_->handle, "COMMIT", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        impl_->last_error = errmsg ? errmsg : "COMMIT failed";
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }
    return true;
#else
    return true;
#endif
}

bool SQLiteConnection::rollback()
{
#ifdef BREEZE_HAVE_SQLITE
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return false;
    }
    char* errmsg = nullptr;
    int rc = sqlite3_exec(impl_->handle, "ROLLBACK", nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        impl_->last_error = errmsg ? errmsg : "ROLLBACK failed";
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }
    return true;
#else
    return true;
#endif
}

bool SQLiteConnection::createDatabase(const std::string& name)
{
    // SQLite databases are files; creating a database means creating/opening a file
    (void)name;
    return true;
}

bool SQLiteConnection::dropDatabase(const std::string& name)
{
    // SQLite: drop database means delete the file
    // This is dangerous, so we just return true for now
    (void)name;
    return true;
}

bool SQLiteConnection::useDatabase(const std::string& name)
{
    // SQLite: attach database
#ifdef BREEZE_HAVE_SQLITE
    if (!impl_->handle) return false;
    std::string q = "ATTACH DATABASE '" + name + "' AS attached_db";
    char* errmsg = nullptr;
    int rc = sqlite3_exec(impl_->handle, q.c_str(), nullptr, nullptr, &errmsg);
    if (errmsg) sqlite3_free(errmsg);
    return rc == SQLITE_OK;
#else
    (void)name;
    return true;
#endif
}

bool SQLiteConnection::createTable(const std::string& name, const std::string& schema)
{
    std::string q = "CREATE TABLE IF NOT EXISTS \"" + name + "\" (" + schema + ")";
    return execute(q) >= 0;
}

bool SQLiteConnection::dropTable(const std::string& name)
{
    std::string q = "DROP TABLE IF EXISTS \"" + name + "\"";
    return execute(q) >= 0;
}

bool SQLiteConnection::truncateTable(const std::string& name)
{
    // SQLite doesn't have TRUNCATE, use DELETE
    std::string q = "DELETE FROM \"" + name + "\"";
    return execute(q) >= 0;
}

std::string SQLiteConnection::escapeString(const std::string& str)
{
    // SQLite uses single quotes, escape by doubling them
    std::string out;
    out.reserve(str.size() + 10);
    for (char c : str) {
        if (c == '\'') {
            out += "''";
        } else {
            out += c;
        }
    }
    return out;
}

std::string SQLiteConnection::getLastError() const
{
    return impl_->last_error;
}

int64_t SQLiteConnection::getLastInsertId() const
{
    return impl_->last_insert_id;
}

int SQLiteConnection::getAffectedRows() const
{
    return impl_->affected_rows;
}

std::vector<Model> SQLiteConnection::find(const Query& query)
{
    auto res = executeQuery(query.to_sql());
    if (!res) return {};
    return utils::resultSetToVector<Model>(res);
}

bool SQLiteConnection::insert(const std::string& table, const Model& model)
{
    if (model.empty()) {
        impl_->last_error = "Cannot insert empty model";
        return false;
    }

    std::ostringstream keys;
    std::ostringstream vals;
    bool first = true;

    for (const auto& [key, value] : model) {
        if (!first) {
            keys << ", ";
            vals << ", ";
        }
        first = false;
        keys << "\"" << key << "\"";
        vals << "'" << escapeString(value) << "'";
    }

    std::string query = "INSERT INTO \"" + table + "\" (" + keys.str() + ") VALUES (" + vals.str() + ")";
    return execute(query) >= 0;
}

bool SQLiteConnection::update(const std::string& table, const Model& model, const Query& where)
{
    if (model.empty()) {
        impl_->last_error = "Cannot update with empty model";
        return false;
    }

    std::ostringstream setClause;
    bool first = true;

    for (const auto& [key, value] : model) {
        if (!first) {
            setClause << ", ";
        }
        first = false;
        setClause << "\"" << key << "\" = '" << escapeString(value) << "'";
    }

    std::string whereClause = where.toWhereClause();
    std::string query = "UPDATE \"" + table + "\" SET " + setClause.str();
    if (!whereClause.empty()) {
        query += " " + whereClause;
    }

    return execute(query) >= 0;
}

bool SQLiteConnection::remove(const std::string& table, const Query& where)
{
    std::string whereClause = where.toWhereClause();
    std::string q = "DELETE FROM \"" + table + "\"";
    if (!whereClause.empty()) {
        q += " " + whereClause;
    }
    return execute(q) >= 0;
}

void SQLiteConnection::executeMigration(const std::string& sql)
{
    execute(sql);
}

} // namespace breeze::database

