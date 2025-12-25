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
    wheres_.push_back(std::move(column) + " " + std::move(op) + " '" + std::move(value) + "'");
    return *this;
}

std::string Query::to_sql() const
{
    std::ostringstream oss;
    oss << "select * from " << table_;
    if (!wheres_.empty()) {
        oss << " where ";
        for (std::size_t i = 0; i < wheres_.size(); ++i) {
            if (i != 0) {
                oss << " and ";
            }
            oss << wheres_[i];
        }
    }
    return oss.str();
}

} // namespace breeze::database
