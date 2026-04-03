#include <breeze/database/query.hpp>

#include <sstream>
#include <utility>

namespace breeze::database {

Query& Query::table(std::string name)
{
    table_ = std::move(name);
    return *this;
}

Query& Query::where(std::string column, std::string op, std::string value)
{
    std::string clause = std::move(column) + " " + std::move(op) + " '" + std::move(value) + "'";
    if (wheres_.empty()) {
        wheres_.push_back(std::move(clause));
    } else {
        wheres_.push_back("AND " + std::move(clause));
    }
    return *this;
}

Query& Query::whereRaw(std::string raw)
{
    if (wheres_.empty()) {
        wheres_.push_back(std::move(raw));
    } else {
        wheres_.push_back("AND " + std::move(raw));
    }
    return *this;
}

Query& Query::orWhere(std::string column, std::string op, std::string value)
{
    std::string clause = std::move(column) + " " + std::move(op) + " '" + std::move(value) + "'";
    if (wheres_.empty()) {
        wheres_.push_back(std::move(clause));
    } else {
        wheres_.push_back("OR " + std::move(clause));
    }
    return *this;
}

Query& Query::whereNull(std::string column)
{
    std::string clause = std::move(column) + " IS NULL";
    if (wheres_.empty()) {
        wheres_.push_back(std::move(clause));
    } else {
        wheres_.push_back("AND " + std::move(clause));
    }
    return *this;
}

Query& Query::whereNotNull(std::string column)
{
    std::string clause = std::move(column) + " IS NOT NULL";
    if (wheres_.empty()) {
        wheres_.push_back(std::move(clause));
    } else {
        wheres_.push_back("AND " + std::move(clause));
    }
    return *this;
}

Query& Query::whereIn(std::string column, const std::vector<std::string>& values)
{
    std::ostringstream oss;
    oss << column << " IN (";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << "'" << values[i] << "'";
    }
    oss << ")";
    std::string clause = oss.str();
    if (wheres_.empty()) {
        wheres_.push_back(std::move(clause));
    } else {
        wheres_.push_back("AND " + std::move(clause));
    }
    return *this;
}

Query& Query::whereBetween(std::string column, std::string low, std::string high)
{
    std::string clause = std::move(column) + " BETWEEN '" + std::move(low) + "' AND '" + std::move(high) + "'";
    if (wheres_.empty()) {
        wheres_.push_back(std::move(clause));
    } else {
        wheres_.push_back("AND " + std::move(clause));
    }
    return *this;
}

Query& Query::select(std::vector<std::string> columns)
{
    columns_ = std::move(columns);
    return *this;
}

Query& Query::selectRaw(std::string raw)
{
    columns_.clear();
    columns_.push_back(std::move(raw));
    return *this;
}

Query& Query::orderBy(std::string column, std::string direction)
{
    orders_.push_back(std::move(column) + " " + std::move(direction));
    return *this;
}

Query& Query::limit(int count)
{
    limit_ = count;
    return *this;
}

Query& Query::offset(int count)
{
    offset_ = count;
    return *this;
}

Query& Query::groupBy(std::string column)
{
    groups_.push_back(std::move(column));
    return *this;
}

Query& Query::having(std::string column, std::string op, std::string value)
{
    havings_.push_back(std::move(column) + " " + std::move(op) + " '" + std::move(value) + "'");
    return *this;
}

Query& Query::join(std::string table, std::string first, std::string op, std::string second)
{
    joins_.push_back("JOIN " + std::move(table) + " ON " + std::move(first) + " " + std::move(op) + " " + std::move(second));
    return *this;
}

Query& Query::leftJoin(std::string table, std::string first, std::string op, std::string second)
{
    joins_.push_back("LEFT JOIN " + std::move(table) + " ON " + std::move(first) + " " + std::move(op) + " " + std::move(second));
    return *this;
}

Query& Query::rightJoin(std::string table, std::string first, std::string op, std::string second)
{
    joins_.push_back("RIGHT JOIN " + std::move(table) + " ON " + std::move(first) + " " + std::move(op) + " " + std::move(second));
    return *this;
}

std::string Query::toWhereClause() const
{
    if (wheres_.empty()) {
        return "";
    }

    std::ostringstream oss;
    oss << "WHERE ";
    for (size_t i = 0; i < wheres_.size(); ++i) {
        if (i > 0) oss << " ";
        oss << wheres_[i];
    }
    return oss.str();
}

std::string Query::toSelectSql() const
{
    std::ostringstream oss;
    oss << "SELECT ";

    // Columns
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << columns_[i];
    }

    oss << " FROM " << table_;

    // JOINs
    for (const auto& join : joins_) {
        oss << " " << join;
    }

    // WHERE
    if (!wheres_.empty()) {
        oss << " " << toWhereClause();
    }

    // GROUP BY
    if (!groups_.empty()) {
        oss << " GROUP BY ";
        for (size_t i = 0; i < groups_.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << groups_[i];
        }
    }

    // HAVING
    if (!havings_.empty()) {
        oss << " HAVING ";
        for (size_t i = 0; i < havings_.size(); ++i) {
            if (i > 0) oss << " AND ";
            oss << havings_[i];
        }
    }

    // ORDER BY
    if (!orders_.empty()) {
        oss << " ORDER BY ";
        for (size_t i = 0; i < orders_.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << orders_[i];
        }
    }

    // LIMIT/OFFSET
    if (limit_) {
        oss << " LIMIT " << *limit_;
    }
    if (offset_) {
        oss << " OFFSET " << *offset_;
    }

    return oss.str();
}

std::string Query::toCountSql() const
{
    std::ostringstream oss;
    oss << "SELECT COUNT(*) FROM " << table_;

    for (const auto& join : joins_) {
        oss << " " << join;
    }

    if (!wheres_.empty()) {
        oss << " " << toWhereClause();
    }

    if (!groups_.empty()) {
        oss << " GROUP BY ";
        for (size_t i = 0; i < groups_.size(); ++i) {
            if (i > 0) oss << ", ";
            oss << groups_[i];
        }
    }

    return oss.str();
}

std::string Query::toDeleteSql() const
{
    std::ostringstream oss;
    oss << "DELETE FROM " << table_;

    if (!wheres_.empty()) {
        oss << " " << toWhereClause();
    }

    return oss.str();
}

std::string Query::toUpdateSql(const std::vector<std::pair<std::string, std::string>>& values) const
{
    std::ostringstream oss;
    oss << "UPDATE " << table_ << " SET ";

    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) oss << ", ";
        oss << values[i].first << " = '" << values[i].second << "'";
    }

    if (!wheres_.empty()) {
        oss << " " << toWhereClause();
    }

    return oss.str();
}

std::string Query::to_sql() const
{
    return toSelectSql();
}

} // namespace breeze::database
