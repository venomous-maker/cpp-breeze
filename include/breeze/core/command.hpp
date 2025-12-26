#pragma once
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>

namespace breeze::core {

class Command {
public:
    struct Option {
        std::string name;
        std::string description;
        std::string default_value;
        bool required = false;
    };

    virtual ~Command() = default;
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual std::vector<Option> options() const { return {}; }
    virtual int handle(const std::unordered_map<std::string, std::string>& options) = 0;
};

class CommandRegistry {
public:
    void register_command(std::shared_ptr<Command> command) {
        commands_[command->name()] = command;
    }

    std::shared_ptr<Command> get(const std::string& name) {
        auto it = commands_.find(name);
        return it != commands_.end() ? it->second : nullptr;
    }

    const std::unordered_map<std::string, std::shared_ptr<Command>>& all() const {
        return commands_;
    }

private:
    std::unordered_map<std::string, std::shared_ptr<Command>> commands_;
};

} // namespace breeze::core
