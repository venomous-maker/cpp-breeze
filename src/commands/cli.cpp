#include <iostream>
#include <vector>
#include <string>
#include <unordered_map>
#include <breeze/core/command.hpp>
#include <breeze/commands/serve_command.hpp>

int main(int argc, char** argv) {
    breeze::core::CommandRegistry registry;
    registry.register_command(std::make_shared<breeze::commands::ServeCommand>());

    if (argc < 2) {
        std::cout << "Breeze Framework CLI\n\n";
        std::cout << "Usage:\n  command [options] [arguments]\n\n";
        std::cout << "Available commands:\n";
        for (const auto& [name, cmd] : registry.all()) {
            std::cout << "  " << name << "    " << cmd->description() << "\n";
        }
        return 0;
    }

    std::string command_name = argv[1];
    auto command = registry.get(command_name);

    if (!command) {
        std::cerr << "Command \"" << command_name << "\" not found.\n";
        return 1;
    }

    std::unordered_map<std::string, std::string> options;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.substr(0, 2) == "--") {
            auto eq_pos = arg.find('=');
            if (eq_pos != std::string::npos) {
                options[arg.substr(2, eq_pos - 2)] = arg.substr(eq_pos + 1);
            } else if (i + 1 < argc && argv[i+1][0] != '-') {
                options[arg.substr(2)] = argv[++i];
            } else {
                options[arg.substr(2)] = "true";
            }
        }
    }

    return command->handle(options);
}
