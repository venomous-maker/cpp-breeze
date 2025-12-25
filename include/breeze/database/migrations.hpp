#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace breeze::database {

struct Migration {
    std::string name;
    std::function<void()> up;
    std::function<void()> down;
};

class Migrator {
public:
    void add(Migration migration) { migrations_.push_back(std::move(migration)); }

    void run_up() const
    {
        for (const auto& m : migrations_) {
            if (m.up) {
                m.up();
            }
        }
    }

    void run_down() const
    {
        for (auto it = migrations_.rbegin(); it != migrations_.rend(); ++it) {
            if (it->down) {
                it->down();
            }
        }
    }

private:
    std::vector<Migration> migrations_;
};

} // namespace breeze::database
