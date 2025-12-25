#pragma once

#include <string>
#include <utility>
#include <vector>

namespace breeze::database {

class Query {
public:
    Query& table(std::string name);

    Query& where(std::string column, std::string op, std::string value);

    std::string to_sql() const;

private:
    std::string table_;
    std::vector<std::string> wheres_;
};

} // namespace breeze::database
