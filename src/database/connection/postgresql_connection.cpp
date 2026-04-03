#include <breeze/database/connection/postgresql_connection.hpp>

#ifdef __has_include
# if __has_include(<libpq-fe.h>)
#  define BREEZE_HAVE_PGSQL 1
#  include <libpq-fe.h>
# elif __has_include(<postgresql/libpq-fe.h>)
#  define BREEZE_HAVE_PGSQL 1
#  include <postgresql/libpq-fe.h>
# endif
#endif

#include <sstream>
#include <algorithm>
#include <cstring>
#include <breeze/database/connection/utils.hpp>

namespace breeze::database {

class PostgreSQLConnection::Impl {
public:
#ifdef BREEZE_HAVE_PGSQL
    Impl() : handle(nullptr), last_error(), connected(false), last_insert_id(0), affected_rows(0) {}
    PGconn* handle;
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

#ifdef BREEZE_HAVE_PGSQL
class PostgreSQLResultSet : public IResultSet {
public:
    explicit PostgreSQLResultSet(PGresult* res)
        : res_(res), current_row_(-1), num_rows_(0), num_cols_(0) {
        if (res_) {
            num_rows_ = PQntuples(res_);
            num_cols_ = PQnfields(res_);
        }
    }

    ~PostgreSQLResultSet() override {
        if (res_) PQclear(res_);
    }

    bool next() override {
        if (!res_) return false;
        ++current_row_;
        return current_row_ < num_rows_;
    }

    int columnCount() const override {
        return num_cols_;
    }

    std::string columnName(int index) const override {
        if (!res_ || index < 0 || index >= num_cols_) return std::string();
        const char* name = PQfname(res_, index);
        return name ? std::string(name) : std::string();
    }

    Value get(int index) const override {
        if (!res_ || current_row_ < 0 || current_row_ >= num_rows_) return nullptr;
        if (index < 0 || index >= num_cols_) return nullptr;

        if (PQgetisnull(res_, current_row_, index)) {
            return nullptr;
        }

        const char* value = PQgetvalue(res_, current_row_, index);
        int len = PQgetlength(res_, current_row_, index);
        Oid type = PQftype(res_, index);

        // PostgreSQL type OIDs
        // INT2OID = 21, INT4OID = 23, INT8OID = 20
        // FLOAT4OID = 700, FLOAT8OID = 701
        // NUMERICOID = 1700
        // BOOLOID = 16
        // BYTEAOID = 17
        // TEXTOID = 25, VARCHAROID = 1043, CHAROID = 18

        switch (type) {
            case 21:  // INT2
            case 23:  // INT4
            case 20:  // INT8
                try { return static_cast<int64_t>(std::stoll(std::string(value, len))); }
                catch (...) { return std::string(value, len); }
            case 700: // FLOAT4
            case 701: // FLOAT8
            case 1700: // NUMERIC
                try { return std::stod(std::string(value, len)); }
                catch (...) { return std::string(value, len); }
            case 16:  // BOOL
                return (value[0] == 't' || value[0] == 'T' || value[0] == '1');
            case 17: { // BYTEA
                // PQ returns bytea in escaped format or binary depending on settings
                // For simplicity, treat as vector<char>
                return std::vector<char>(value, value + len);
            }
            default:
                return std::string(value, len);
        }
    }

    Value get(const std::string& column) const override {
        if (!res_) return nullptr;
        int col = PQfnumber(res_, column.c_str());
        if (col < 0) return nullptr;
        return get(col);
    }

private:
    PGresult* res_;
    int current_row_;
    int num_rows_;
    int num_cols_;
};
#endif

PostgreSQLConnection::PostgreSQLConnection(const DatabaseConfig& config)
    : impl_(std::make_unique<Impl>())
{
    impl_->cfg = config;
}

PostgreSQLConnection::~PostgreSQLConnection() {
    disconnect();
}

bool PostgreSQLConnection::connect()
{
#ifdef BREEZE_HAVE_PGSQL
    if (impl_->connected && impl_->handle) return true;

    // Build connection string
    std::ostringstream connStr;
    if (!impl_->cfg.host.empty()) {
        connStr << "host=" << impl_->cfg.host << " ";
    }
    if (!impl_->cfg.port.empty()) {
        connStr << "port=" << impl_->cfg.port << " ";
    }
    if (!impl_->cfg.database.empty()) {
        connStr << "dbname=" << impl_->cfg.database << " ";
    }
    if (!impl_->cfg.username.empty()) {
        connStr << "user=" << impl_->cfg.username << " ";
    }
    if (!impl_->cfg.password.empty()) {
        connStr << "password=" << impl_->cfg.password << " ";
    }

    impl_->handle = PQconnectdb(connStr.str().c_str());
    if (PQstatus(impl_->handle) != CONNECTION_OK) {
        impl_->last_error = PQerrorMessage(impl_->handle);
        PQfinish(impl_->handle);
        impl_->handle = nullptr;
        impl_->connected = false;
        return false;
    }

    impl_->connected = true;
    return true;
#else
    impl_->connected = true;
    return true;
#endif
}

void PostgreSQLConnection::disconnect()
{
#ifdef BREEZE_HAVE_PGSQL
    if (impl_->handle) {
        PQfinish(impl_->handle);
        impl_->handle = nullptr;
    }
    impl_->connected = false;
#else
    impl_->connected = false;
#endif
}

bool PostgreSQLConnection::isConnected() const
{
    return impl_->connected;
}

bool PostgreSQLConnection::ping()
{
#ifdef BREEZE_HAVE_PGSQL
    if (!impl_->handle) return false;
    return PQstatus(impl_->handle) == CONNECTION_OK;
#else
    return impl_->connected;
#endif
}

std::unique_ptr<IResultSet> PostgreSQLConnection::executeQuery(const std::string& query)
{
#ifdef BREEZE_HAVE_PGSQL
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return nullptr;
    }

    PGresult* res = PQexec(impl_->handle, query.c_str());
    ExecStatusType status = PQresultStatus(res);

    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        impl_->last_error = PQerrorMessage(impl_->handle);
        PQclear(res);
        return nullptr;
    }

    return std::make_unique<PostgreSQLResultSet>(res);
#else
    (void)query;
    return nullptr;
#endif
}

int PostgreSQLConnection::executeUpdate(const std::string& query)
{
#ifdef BREEZE_HAVE_PGSQL
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return 0;
    }

    PGresult* res = PQexec(impl_->handle, query.c_str());
    ExecStatusType status = PQresultStatus(res);

    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        impl_->last_error = PQerrorMessage(impl_->handle);
        PQclear(res);
        return 0;
    }

    const char* affected = PQcmdTuples(res);
    impl_->affected_rows = affected ? std::atoi(affected) : 0;

    // Try to get last insert id from RETURNING clause or OID
    Oid oid = PQoidValue(res);
    if (oid != InvalidOid) {
        impl_->last_insert_id = static_cast<int64_t>(oid);
    }

    PQclear(res);
    return impl_->affected_rows;
#else
    (void)query;
    return 0;
#endif
}

int PostgreSQLConnection::execute(const std::string& query)
{
    return executeUpdate(query);
}

std::unique_ptr<IResultSet> PostgreSQLConnection::executePrepared(
    const std::string& query, const std::vector<Value>& params)
{
#ifdef BREEZE_HAVE_PGSQL
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return nullptr;
    }

    // Convert params to C strings
    std::vector<std::string> paramStrings;
    std::vector<const char*> paramValues;
    paramStrings.reserve(params.size());
    paramValues.reserve(params.size());

    for (const auto& param : params) {
        paramStrings.push_back(utils::valueToString(param));
    }
    for (const auto& s : paramStrings) {
        paramValues.push_back(s.c_str());
    }

    // Convert ? placeholders to $1, $2, etc.
    std::string pgQuery;
    pgQuery.reserve(query.size() + params.size() * 2);
    int paramNum = 0;
    for (size_t i = 0; i < query.size(); ++i) {
        if (query[i] == '?') {
            pgQuery += "$" + std::to_string(++paramNum);
        } else {
            pgQuery += query[i];
        }
    }

    PGresult* res = PQexecParams(
        impl_->handle,
        pgQuery.c_str(),
        static_cast<int>(params.size()),
        nullptr,  // Let PostgreSQL infer types
        paramValues.data(),
        nullptr,  // text format
        nullptr,  // text format
        0         // text result
    );

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        impl_->last_error = PQerrorMessage(impl_->handle);
        PQclear(res);
        return nullptr;
    }

    return std::make_unique<PostgreSQLResultSet>(res);
#else
    (void)query; (void)params;
    return nullptr;
#endif
}

int PostgreSQLConnection::executePreparedUpdate(
    const std::string& query, const std::vector<Value>& params)
{
#ifdef BREEZE_HAVE_PGSQL
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return 0;
    }

    std::vector<std::string> paramStrings;
    std::vector<const char*> paramValues;
    paramStrings.reserve(params.size());
    paramValues.reserve(params.size());

    for (const auto& param : params) {
        paramStrings.push_back(utils::valueToString(param));
    }
    for (const auto& s : paramStrings) {
        paramValues.push_back(s.c_str());
    }

    // Convert ? to $1, $2, etc.
    std::string pgQuery;
    pgQuery.reserve(query.size() + params.size() * 2);
    int paramNum = 0;
    for (size_t i = 0; i < query.size(); ++i) {
        if (query[i] == '?') {
            pgQuery += "$" + std::to_string(++paramNum);
        } else {
            pgQuery += query[i];
        }
    }

    PGresult* res = PQexecParams(
        impl_->handle,
        pgQuery.c_str(),
        static_cast<int>(params.size()),
        nullptr,
        paramValues.data(),
        nullptr,
        nullptr,
        0
    );

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        impl_->last_error = PQerrorMessage(impl_->handle);
        PQclear(res);
        return 0;
    }

    const char* affected = PQcmdTuples(res);
    impl_->affected_rows = affected ? std::atoi(affected) : 0;

    Oid oid = PQoidValue(res);
    if (oid != InvalidOid) {
        impl_->last_insert_id = static_cast<int64_t>(oid);
    }

    PQclear(res);
    return impl_->affected_rows;
#else
    (void)query; (void)params;
    return 0;
#endif
}

bool PostgreSQLConnection::beginTransaction()
{
#ifdef BREEZE_HAVE_PGSQL
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return false;
    }
    PGresult* res = PQexec(impl_->handle, "BEGIN");
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) impl_->last_error = PQerrorMessage(impl_->handle);
    PQclear(res);
    return ok;
#else
    return true;
#endif
}

bool PostgreSQLConnection::commit()
{
#ifdef BREEZE_HAVE_PGSQL
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return false;
    }
    PGresult* res = PQexec(impl_->handle, "COMMIT");
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) impl_->last_error = PQerrorMessage(impl_->handle);
    PQclear(res);
    return ok;
#else
    return true;
#endif
}

bool PostgreSQLConnection::rollback()
{
#ifdef BREEZE_HAVE_PGSQL
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return false;
    }
    PGresult* res = PQexec(impl_->handle, "ROLLBACK");
    bool ok = PQresultStatus(res) == PGRES_COMMAND_OK;
    if (!ok) impl_->last_error = PQerrorMessage(impl_->handle);
    PQclear(res);
    return ok;
#else
    return true;
#endif
}

bool PostgreSQLConnection::createDatabase(const std::string& name)
{
    std::string q = "CREATE DATABASE \"" + name + "\"";
    return execute(q) >= 0;
}

bool PostgreSQLConnection::dropDatabase(const std::string& name)
{
    std::string q = "DROP DATABASE IF EXISTS \"" + name + "\"";
    return execute(q) >= 0;
}

bool PostgreSQLConnection::useDatabase(const std::string& name)
{
    // PostgreSQL doesn't support USE database; need to reconnect
    // For now, just update the config and reconnect
    impl_->cfg.database = name;
    disconnect();
    return connect();
}

bool PostgreSQLConnection::createTable(const std::string& name, const std::string& schema)
{
    std::string q = "CREATE TABLE IF NOT EXISTS \"" + name + "\" (" + schema + ")";
    return execute(q) >= 0;
}

bool PostgreSQLConnection::dropTable(const std::string& name)
{
    std::string q = "DROP TABLE IF EXISTS \"" + name + "\"";
    return execute(q) >= 0;
}

bool PostgreSQLConnection::truncateTable(const std::string& name)
{
    std::string q = "TRUNCATE TABLE \"" + name + "\"";
    return execute(q) >= 0;
}

std::string PostgreSQLConnection::escapeString(const std::string& str)
{
#ifdef BREEZE_HAVE_PGSQL
    if (!impl_->handle) {
        // Fallback escape
        std::string out;
        out.reserve(str.size() + 10);
        for (char c : str) {
            if (c == '\'') out += "''";
            else if (c == '\\') out += "\\\\";
            else out += c;
        }
        return out;
    }

    char* escaped = PQescapeLiteral(impl_->handle, str.c_str(), str.size());
    if (!escaped) {
        impl_->last_error = PQerrorMessage(impl_->handle);
        return str;
    }
    std::string result(escaped);
    PQfreemem(escaped);
    // Remove surrounding quotes that PQescapeLiteral adds
    if (result.size() >= 2 && result.front() == '\'' && result.back() == '\'') {
        return result.substr(1, result.size() - 2);
    }
    return result;
#else
    std::string out;
    out.reserve(str.size() + 10);
    for (char c : str) {
        if (c == '\'') out += "''";
        else if (c == '\\') out += "\\\\";
        else out += c;
    }
    return out;
#endif
}

std::string PostgreSQLConnection::getLastError() const
{
    return impl_->last_error;
}

int64_t PostgreSQLConnection::getLastInsertId() const
{
    return impl_->last_insert_id;
}

int PostgreSQLConnection::getAffectedRows() const
{
    return impl_->affected_rows;
}

std::vector<Model> PostgreSQLConnection::find(const Query& query)
{
    auto res = executeQuery(query.to_sql());
    if (!res) return {};
    return utils::resultSetToVector<Model>(res);
}

bool PostgreSQLConnection::insert(const std::string& table, const Model& model)
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

    std::string query = "INSERT INTO \"" + table + "\" (" + keys.str() + ") VALUES (" + vals.str() + ") RETURNING id";

    // Try to capture the returned id
#ifdef BREEZE_HAVE_PGSQL
    if (!impl_->handle) {
        impl_->last_error = "Not connected";
        return false;
    }

    PGresult* res = PQexec(impl_->handle, query.c_str());
    ExecStatusType status = PQresultStatus(res);

    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        impl_->last_error = PQerrorMessage(impl_->handle);
        PQclear(res);
        // Try without RETURNING
        query = "INSERT INTO \"" + table + "\" (" + keys.str() + ") VALUES (" + vals.str() + ")";
        return execute(query) >= 0;
    }

    // Get the returned id if available
    if (PQntuples(res) > 0 && PQnfields(res) > 0) {
        const char* idStr = PQgetvalue(res, 0, 0);
        if (idStr) {
            try { impl_->last_insert_id = std::stoll(idStr); } catch (...) {}
        }
    }

    PQclear(res);
    return true;
#else
    return execute(query) >= 0;
#endif
}

bool PostgreSQLConnection::update(const std::string& table, const Model& model, const Query& where)
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

bool PostgreSQLConnection::remove(const std::string& table, const Query& where)
{
    std::string whereClause = where.toWhereClause();
    std::string q = "DELETE FROM \"" + table + "\"";
    if (!whereClause.empty()) {
        q += " " + whereClause;
    }
    return execute(q) >= 0;
}

void PostgreSQLConnection::executeMigration(const std::string& sql)
{
    execute(sql);
}

} // namespace breeze::database

