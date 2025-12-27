#pragma once
#include <breeze/core/command.hpp>
#include <breeze/core/application.hpp>

namespace breeze::commands {

class ServeCommand : public breeze::core::Command {
public:
    std::string name() const override { return "serve"; }
    std::string description() const override { return "Serve the application on the PHP development server"; }

    std::vector<Option> options() const override {
        return {
            {"host", "The host address to serve the application on", "127.0.0.1"},
            {"port", "The port to serve the application on", "8000"}
        };
    }

    int handle(const std::unordered_map<std::string, std::string>& options) override {
        std::string host = options.count("host") ? options.at("host") : "127.0.0.1";
        int port = options.count("port") ? std::stoi(options.at("port")) : 8000;

        auto app = breeze::core::Application::create();

        // Start server
        app->run(port);
        return 0;
    }
};

} // namespace breeze::commands
