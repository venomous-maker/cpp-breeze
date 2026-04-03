#pragma once

#include "types.hpp"
#include "interfaces.hpp"
#include <string>
#include <vector>
#include <type_traits>

namespace breeze::database::utils {
    std::string valueToString(const Value& value);
    Value stringToValue(const std::string& str, const std::string& type = "string");

    template<typename T>
    std::vector<T> resultSetToVector(std::unique_ptr<IResultSet>& result) {
        std::vector<T> items;
        if (!result) return items;

        while (result->next()) {
            if constexpr (std::is_same_v<T, Model>) {
                Model m;
                int cols = result->columnCount();
                for (int i = 0; i < cols; ++i) {
                    std::string name = result->columnName(i);
                    // convert column value to string when storing in Model
                    Value v = result->get(i);
                    m.set(std::move(name), valueToString(v));
                }
                items.push_back(std::move(m));
            } else {
                // take first column and attempt to convert to T
                if constexpr (std::is_same_v<T, std::string>) {
                    items.push_back(result->getAs<std::string>(0));
                } else {
                    items.push_back(result->getAs<T>(0));
                }
            }
        }
        return items;
    }
}
