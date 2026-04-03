#pragma once

#include <string>
#include <utility>
#include <vector>
#include <optional>

namespace breeze::database {

class Query {
public:
    Query& table(std::string name);

    // WHERE clauses
    Query& where(std::string column, std::string op, std::string value);
    Query& whereRaw(std::string raw);
    Query& orWhere(std::string column, std::string op, std::string value);
    Query& whereNull(std::string column);
    Query& whereNotNull(std::string column);
    Query& whereIn(std::string column, const std::vector<std::string>& values);
    Query& whereBetween(std::string column, std::string low, std::string high);

    // SELECT columns
    Query& select(std::vector<std::string> columns);
    Query& selectRaw(std::string raw);

    // ORDER BY
    Query& orderBy(std::string column, std::string direction = "ASC");

    // LIMIT/OFFSET
    Query& limit(int count);
    Query& offset(int count);

    // GROUP BY / HAVING
    Query& groupBy(std::string column);
    Query& having(std::string column, std::string op, std::string value);

    // JOIN support
    Query& join(std::string table, std::string first, std::string op, std::string second);
    Query& leftJoin(std::string table, std::string first, std::string op, std::string second);
    Query& rightJoin(std::string table, std::string first, std::string op, std::string second);

    // Generate SQL
    std::string to_sql() const;
    std::string toWhereClause() const;  // Just the WHERE part for UPDATE/DELETE
    std::string toSelectSql() const;    // Full SELECT query
    std::string toCountSql() const;     // SELECT COUNT(*) query
    std::string toDeleteSql() const;    // DELETE query
    std::string toUpdateSql(const std::vector<std::pair<std::string, std::string>>& values) const;

    // Accessors
    const std::string& getTable() const { return table_; }
    bool hasWheres() const { return !wheres_.empty(); }

private:
    std::string table_;
    std::vector<std::string> columns_{"*"};
    std::vector<std::string> wheres_;
    std::vector<std::string> orders_;
    std::vector<std::string> groups_;
    std::vector<std::string> havings_;
    std::vector<std::string> joins_;
    std::optional<int> limit_;
    std::optional<int> offset_;
};

} // namespace breeze::database
