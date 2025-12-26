#pragma once

#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>
#include <breeze/http/status_code.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <stdexcept>

namespace breeze::http {

/**
 * Basic HTTP Server implementation for Breeze.
 * Handles socket-level communication, basic HTTP parsing, and multi-threaded request dispatching.
 */
class Server {
public:
    using RequestHandler = std::function<Response(const Request&)>;

    explicit Server(RequestHandler handler) : handler_(std::move(handler)) {}

    [[noreturn]] void listen(const std::string& host, int port) {
        int server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            throw std::runtime_error("Failed to create socket");
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = inet_addr(host.c_str());
        address.sin_port = htons(port);

        if (bind(server_fd, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) < 0) {
            close(server_fd);
            throw std::runtime_error("Failed to bind to " + host + ":" + std::to_string(port));
        }

        if (::listen(server_fd, 10) < 0) {
            close(server_fd);
            throw std::runtime_error("Failed to listen");
        }

        std::cout << "Server listening on http://" << host << ":" << port << std::endl;

        while (true) {
            sockaddr_in client_address{};
            socklen_t client_len = sizeof(client_address);
            int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
            if (client_fd < 0) {
                continue;
            }

            std::thread([this, client_fd]() {
                handle_client(client_fd);
            }).detach();
        }
    }

private:
    void handle_client(int client_fd) {
        char buffer[4096] = {0};
        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer));
        if (bytes_read <= 0) {
            close(client_fd);
            return;
        }

        std::string raw_request(buffer, bytes_read);
        Request req = parse_request(raw_request);
        Response res = handler_(req);
        
        std::string raw_response = res.to_string();
        send(client_fd, raw_response.c_str(), raw_response.size(), 0);
        close(client_fd);
    }

    static Request parse_request(const std::string& raw) {
        Request req;
        std::istringstream stream(raw);
        std::string line;
        
        // Request line
        if (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::istringstream line_stream(line);
            std::string method, path, version;
            line_stream >> method >> path >> version;
            req.set_method(method);
            
            size_t query_pos = path.find('?');
            if (query_pos != std::string::npos) {
                req.set_path(path.substr(0, query_pos));
                req.set_query_string(path.substr(query_pos + 1));
            } else {
                req.set_path(path);
            }
        }

        // Headers
        while (std::getline(stream, line) && line != "\r" && !line.empty()) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = line.substr(0, colon);
                std::string value = line.substr(colon + 1);
                // Trim value
                value.erase(0, value.find_first_not_of(' '));
                req.set_header(name, value);
            }
        }

        // Body
        std::string body((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        req.set_body(body);

        return req;
    }

    RequestHandler handler_;
};

} // namespace breeze::http
