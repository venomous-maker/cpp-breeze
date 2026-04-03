#include <breeze/database/connection/mysql_connection.hpp>

#ifdef __has_include
# if __has_include(<mysql/mysql.h>)
#  define BREEZE_HAVE_MYSQL 1
#  include <mysql/mysql.h>
# endif
#endif

#include <sstream>
#include <algorithm>
#include <cstring>
#include <breeze/database/connection/utils.hpp>

namespace breeze::database {

class MySQLConnection::Impl {
public:
#ifdef BREEZE_HAVE_MYSQL
    Impl() : handle(nullptr), last_error(), connected(false), last_insert_id(0), affected_rows(0) {}
    MYSQL* handle;
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

// ResultSet wrapper for MYSQL_RES
#ifdef BREEZE_HAVE_MYSQL
class MySQLResultSet : public IResultSet {
public:
    explicit MySQLResultSet(MYSQL_RES* res)
        : res_(res), row_(nullptr), lengths_(nullptr), fetched(false) {}

    ~MySQLResultSet() override {
        if (res_) mysql_free_result(res_);
    }

    bool next() override {
        if (!res_) return false;
        row_ = mysql_fetch_row(res_);
        if (!row_) return false;
        lengths_ = mysql_fetch_lengths(res_);
        return true;
    }

    int columnCount() const override {
        if (!res_) return 0;
        return static_cast<int>(mysql_num_fields(res_));
    }

    std::string columnName(int index) const override {
        if (!res_) return std::string();
        MYSQL_FIELD* f = mysql_fetch_field_direct(res_, index);
        return f ? std::string(f->name) : std::string();
    }

    Value get(int index) const override {
        if (!res_ || !row_) return nullptr;
        if (index < 0) return nullptr;
        unsigned long len = lengths_ ? lengths_[index] : 0;
        if (!row_[index]) return nullptr;
        MYSQL_FIELD* f = mysql_fetch_field_direct(res_, index);
        if (!f) return std::string(row_[index], row_[index] + len);

        switch (f->type) {
            case MYSQL_TYPE_TINY:
            case MYSQL_TYPE_SHORT:
            case MYSQL_TYPE_LONG:
            case MYSQL_TYPE_INT24:
            case MYSQL_TYPE_LONGLONG: {
                try { return static_cast<int64_t>(std::stoll(std::string(row_[index], len))); } catch (...) { return std::string(row_[index], row_[index] + len); }
            }
            case MYSQL_TYPE_FLOAT:
            case MYSQL_TYPE_DOUBLE:
            case MYSQL_TYPE_DECIMAL:
            case MYSQL_TYPE_NEWDECIMAL: {
                try { return std::stod(std::string(row_[index], len)); } catch (...) { return std::string(row_[index], row_[index] + len); }
            }
            case MYSQL_TYPE_BLOB:
            case MYSQL_TYPE_MEDIUM_BLOB:
            case MYSQL_TYPE_LONG_BLOB:
            case MYSQL_TYPE_TINY_BLOB:
            case MYSQL_TYPE_GEOMETRY:
            case MYSQL_TYPE_VAR_STRING:
            case MYSQL_TYPE_STRING: {
                // Treat binary blobs as vector<char>
                if (f->type == MYSQL_TYPE_BLOB || f->type == MYSQL_TYPE_MEDIUM_BLOB || f->type == MYSQL_TYPE_LONG_BLOB || f->type == MYSQL_TYPE_TINY_BLOB) {
                    const char* ptr = row_[index];
                    return std::vector<char>(ptr, ptr + len);
                }
                return std::string(row_[index], row_[index] + len);
            }
            case MYSQL_TYPE_NULL:
                return nullptr;
            default:
                return std::string(row_[index], row_[index] + len);
        }
    }

    Value get(const std::string& column) const override {
        if (!res_ || !row_) return nullptr;
        int cols = columnCount();
        for (int i = 0; i < cols; ++i) {
            MYSQL_FIELD* f = mysql_fetch_field_direct(res_, i);
            if (f && column == f->name) return get(i);
        }
        return nullptr;
    }

private:
    MYSQL_RES* res_;
    MYSQL_ROW row_;
    unsigned long* lengths_;
    bool fetched;
};
#endif

MySQLConnection::MySQLConnection(const DatabaseConfig& config)
    : impl_(std::make_unique<Impl>())
{
    impl_->cfg = config;
}

MySQLConnection::~MySQLConnection() {
    disconnect();
}

bool MySQLConnection::connect()
{
#ifdef BREEZE_HAVE_MYSQL
    if (impl_->connected && impl_->handle) return true;
    impl_->handle = mysql_init(nullptr);
    if (!impl_->handle) {
        impl_->last_error = "mysql_init failed";
        impl_->connected = false;
        return false;
    }

    unsigned int port = 0;
    try { port = static_cast<unsigned int>(std::stoul(impl_->cfg.port)); } catch (...) { port = 0; }

    MYSQL* c = mysql_real_connect(
        impl_->handle,
        impl_->cfg.host.empty() ? nullptr : impl_->cfg.host.c_str(),
        impl_->cfg.username.empty() ? nullptr : impl_->cfg.username.c_str(),
        impl_->cfg.password.empty() ? nullptr : impl_->cfg.password.c_str(),
        impl_->cfg.database.empty() ? nullptr : impl_->cfg.database.c_str(),
        port,
        nullptr,
        0
    );
    if (!c) {
        impl_->last_error = mysql_error(impl_->handle);
        impl_->connected = false;
        mysql_close(impl_->handle);
        impl_->handle = nullptr;
        return false;
    }

    impl_->connected = true;
    return true;
#else
    // No MySQL client available; behave like a stub
    impl_->connected = true;
    return true;
#endif
}

void MySQLConnection::disconnect()
{
#ifdef BREEZE_HAVE_MYSQL
    if (impl_->handle) {
        mysql_close(impl_->handle);
        impl_->handle = nullptr;
    }
    impl_->connected = false;
#else
    impl_->connected = false;
#endif
}

bool MySQLConnection::isConnected() const
{
    return impl_->connected;
}

bool MySQLConnection::ping()
{
#ifdef BREEZE_HAVE_MYSQL
    if (!impl_->handle) return false;
    return mysql_ping(impl_->handle) == 0;
#else
    return impl_->connected;
#endif
}

std::unique_ptr<IResultSet> MySQLConnection::executeQuery(const std::string& query)
{
#ifdef BREEZE_HAVE_MYSQL
    if (!impl_->handle) { impl_->last_error = "Not connected"; return nullptr; }
    if (mysql_query(impl_->handle, query.c_str()) != 0) {
        impl_->last_error = mysql_error(impl_->handle);
        return nullptr;
    }
    MYSQL_RES* res = mysql_store_result(impl_->handle);
    if (!res) return nullptr;
    return std::make_unique<MySQLResultSet>(res);
#else
    (void)query;
    return nullptr;
#endif
}

int MySQLConnection::executeUpdate(const std::string& query)
{
#ifdef BREEZE_HAVE_MYSQL
    if (!impl_->handle) { impl_->last_error = "Not connected"; return 0; }
    if (mysql_query(impl_->handle, query.c_str()) != 0) {
        impl_->last_error = mysql_error(impl_->handle);
        return 0;
    }
    impl_->affected_rows = static_cast<int>(mysql_affected_rows(impl_->handle));
    impl_->last_insert_id = static_cast<int64_t>(mysql_insert_id(impl_->handle));
    return impl_->affected_rows;
#else
    (void)query;
    return 0;
#endif
}

int MySQLConnection::execute(const std::string& query)
{
    return executeUpdate(query);
}

std::unique_ptr<IResultSet> MySQLConnection::executePrepared(const std::string& query, const std::vector<Value>& params)
{
    // Naive parameter substitution: replace each '?' with escaped value. Not secure for production.
#ifdef BREEZE_HAVE_MYSQL
    if (!impl_->handle) { impl_->last_error = "Not connected"; return nullptr; }
    std::string built;
    built.reserve(query.size() + 64);
    size_t param_index = 0;
    for (size_t i = 0; i < query.size(); ++i) {
        if (query[i] == '?' && param_index < params.size()) {
            // convert param to string and escape
            Value v = params[param_index++];
            std::string s = utils::valueToString(v);
            std::string esc; esc.resize(s.size()*2 + 1);
            unsigned long outlen = mysql_real_escape_string(impl_->handle, esc.data(), s.c_str(), static_cast<unsigned long>(s.size()));
            esc.resize(outlen);
            // wrap in quotes
            built += '\'';
            built += esc;
            built += '\'';
        } else {
            built += query[i];
        }
    }
    return executeQuery(built);
#else
    (void)query; (void)params;
    return nullptr;
#endif
}

int MySQLConnection::executePreparedUpdate(const std::string& query, const std::vector<Value>& params)
{
#ifdef BREEZE_HAVE_MYSQL
    if (!impl_->handle) { impl_->last_error = "Not connected"; return 0; }
    std::string built;
    size_t param_index = 0;
    for (size_t i = 0; i < query.size(); ++i) {
        if (query[i] == '?' && param_index < params.size()) {
            Value v = params[param_index++];
            std::string s = utils::valueToString(v);
            std::string esc; esc.resize(s.size()*2 + 1);
            unsigned long outlen = mysql_real_escape_string(impl_->handle, esc.data(), s.c_str(), static_cast<unsigned long>(s.size()));
            esc.resize(outlen);
            built += '\'';
            built += esc;
            built += '\'';
        } else {
            built += query[i];
        }
    }
    return executeUpdate(built);
#else
    (void)query; (void)params;
    return 0;
#endif
}

bool MySQLConnection::beginTransaction()
{
#ifdef BREEZE_HAVE_MYSQL
    if (!impl_->handle) { impl_->last_error = "Not connected"; return false; }
    if (mysql_autocommit(impl_->handle, 0) != 0) {
        impl_->last_error = mysql_error(impl_->handle);
        return false;
    }
    return true;
#else
    return true;
#endif
}

bool MySQLConnection::commit()
{
#ifdef BREEZE_HAVE_MYSQL
    if (!impl_->handle) { impl_->last_error = "Not connected"; return false; }
    if (mysql_commit(impl_->handle) != 0) {
        impl_->last_error = mysql_error(impl_->handle);
        return false;
    }
    // restore autocommit
    mysql_autocommit(impl_->handle, 1);
    return true;
#else
    return true;
#endif
}

bool MySQLConnection::rollback()
{
#ifdef BREEZE_HAVE_MYSQL
    if (!impl_->handle) { impl_->last_error = "Not connected"; return false; }
    if (mysql_rollback(impl_->handle) != 0) {
        impl_->last_error = mysql_error(impl_->handle);
        return false;
    }
    mysql_autocommit(impl_->handle, 1);
    return true;
#else
    return true;
#endif
}

bool MySQLConnection::createDatabase(const std::string& name)
{
    std::string q = "CREATE DATABASE IF NOT EXISTS `" + name + "`";
    return execute(q) >= 0;
}

bool MySQLConnection::dropDatabase(const std::string& name)
{
    std::string q = "DROP DATABASE IF EXISTS `" + name + "`";
    return execute(q) >= 0;
}

bool MySQLConnection::useDatabase(const std::string& name)
{
#ifdef BREEZE_HAVE_MYSQL
    if (!impl_->handle) return false;
    std::string q = "USE `" + name + "`";
    if (mysql_query(impl_->handle, q.c_str()) != 0) { impl_->last_error = mysql_error(impl_->handle); return false; }
    return true;
#else
    (void)name; return true;
#endif
}

bool MySQLConnection::createTable(const std::string& name, const std::string& schema)
{
    std::string q = "CREATE TABLE IF NOT EXISTS `" + name + "` (" + schema + ")";
    return execute(q) >= 0;
}

bool MySQLConnection::dropTable(const std::string& name)
{
    std::string q = "DROP TABLE IF EXISTS `" + name + "`";
    return execute(q) >= 0;
}

bool MySQLConnection::truncateTable(const std::string& name)
{
    std::string q = "TRUNCATE TABLE `" + name + "`";
    return execute(q) >= 0;
}

std::string MySQLConnection::escapeString(const std::string& str)
{
#ifdef BREEZE_HAVE_MYSQL
    if (!impl_->handle) return str;
    std::string esc; esc.resize(str.size()*2 + 1);
    unsigned long outlen = mysql_real_escape_string(impl_->handle, esc.data(), str.c_str(), static_cast<unsigned long>(str.size()));
    esc.resize(outlen);
    return esc;
#else
    // Basic fallback
    std::string s = str;
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\\' || c == '\'' || c == '\"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
#endif
}

std::string MySQLConnection::getLastError() const
{
    return impl_->last_error;
}

int64_t MySQLConnection::getLastInsertId() const
{
    return impl_->last_insert_id;
}

int MySQLConnection::getAffectedRows() const
{
    return impl_->affected_rows;
}

std::vector<Model> MySQLConnection::find(const Query& query)
{
    auto res = executeQuery(query.to_sql());
    if (!res) return {};
    return utils::resultSetToVector<Model>(res);
}

bool MySQLConnection::insert(const std::string& table, const Model& model)
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
        keys << "`" << key << "`";
        vals << "'" << escapeString(value) << "'";
    }

    std::string query = "INSERT INTO `" + table + "` (" + keys.str() + ") VALUES (" + vals.str() + ")";
    return execute(query) >= 0;
}

bool MySQLConnection::update(const std::string& table, const Model& model, const Query& where)
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
        setClause << "`" << key << "` = '" << escapeString(value) << "'";
    }

    std::string whereClause = where.toWhereClause();
    std::string query = "UPDATE `" + table + "` SET " + setClause.str();
    if (!whereClause.empty()) {
        query += " " + whereClause;
    }

    return execute(query) >= 0;
}

bool MySQLConnection::remove(const std::string& table, const Query& where)
{
    std::string whereClause = where.toWhereClause();
    std::string q = "DELETE FROM `" + table + "`";
    if (!whereClause.empty()) {
        q += " " + whereClause;
    }
    return execute(q) >= 0;
}

void MySQLConnection::executeMigration(const std::string& sql)
{
    execute(sql);
}

} // namespace breeze::database

