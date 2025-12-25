#include <breeze/breeze.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

namespace {

std::string reason_phrase(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 404:
        return "Not Found";
    case 500:
        return "Internal Server Error";
    default:
        return "";
    }
}

std::string strip_query(const std::string& path)
{
    const auto pos = path.find('?');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(0, pos);
}

bool query_has_auth_1(const std::string& path)
{
    const auto pos = path.find('?');
    if (pos == std::string::npos) {
        return false;
    }
    const auto query = path.substr(pos + 1);

    std::size_t start = 0;
    while (start < query.size()) {
        const auto amp = query.find('&', start);
        const auto part = query.substr(start, amp == std::string::npos ? std::string::npos : (amp - start));

        const auto eq = part.find('=');
        if (eq != std::string::npos) {
            const auto key = part.substr(0, eq);
            const auto val = part.substr(eq + 1);
            if ((key == "auth" || key == "x-auth") && val == "1") {
                return true;
            }
        }

        if (amp == std::string::npos) {
            break;
        }
        start = amp + 1;
    }

    return false;
}

bool read_http_request(int fd, std::string& out)
{
    out.clear();
    char buf[4096];
    while (out.find("\r\n\r\n") == std::string::npos) {
        const auto n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0) {
            return false;
        }
        out.append(buf, static_cast<std::size_t>(n));
        if (out.size() > 1024 * 1024) {
            return false;
        }
    }
    return true;
}

breeze::http::Response dispatch_http(breeze::core::Application& app, const std::string& raw)
{
    std::istringstream iss(raw);

    std::string request_line;
    if (!std::getline(iss, request_line)) {
        breeze::http::Response res;
        res.set_status(400);
        res.set_body("Bad Request");
        return res;
    }
    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    std::istringstream rl(request_line);
    std::string method;
    std::string target;
    std::string version;
    rl >> method >> target >> version;
    if (method.empty() || target.empty()) {
        breeze::http::Response res;
        res.set_status(400);
        res.set_body("Bad Request");
        return res;
    }

    breeze::http::Request req;
    req.set_method(method);
    req.set_path(strip_query(target));

    if (query_has_auth_1(target)) {
        req.set_header("X-Auth", "1");
    }

    std::string header_line;
    while (std::getline(iss, header_line)) {
        if (header_line == "\r" || header_line.empty()) {
            break;
        }
        if (!header_line.empty() && header_line.back() == '\r') {
            header_line.pop_back();
        }

        const auto colon = header_line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        auto name = header_line.substr(0, colon);
        auto value = header_line.substr(colon + 1);
        while (!value.empty() && value.front() == ' ') {
            value.erase(value.begin());
        }
        req.set_header(std::move(name), std::move(value));
    }

    return app.handle(req);
}

std::string serialize_http_response(const breeze::http::Response& res)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << res.status();
    const auto phrase = reason_phrase(res.status());
    if (!phrase.empty()) {
        oss << " " << phrase;
    }
    oss << "\r\n";

    bool has_content_length = false;
    bool has_content_type = false;
    for (const auto& [k, v] : res.headers()) {
        if (k == "Content-Length") {
            has_content_length = true;
        }
        if (k == "Content-Type") {
            has_content_type = true;
        }
        oss << k << ": " << v << "\r\n";
    }
    if (!has_content_type) {
        oss << "Content-Type: text/plain; charset=utf-8\r\n";
    }
    if (!has_content_length) {
        oss << "Content-Length: " << res.body().size() << "\r\n";
    }
    oss << "Connection: close\r\n";
    oss << "\r\n";
    oss << res.body();
    return oss.str();
}

int serve(breeze::core::Application& app, std::uint16_t port)
{
    const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "socket() failed: " << std::strerror(errno) << "\n";
        return 1;
    }

    int yes = 1;
    ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "bind() failed: " << std::strerror(errno) << "\n";
        ::close(server_fd);
        return 1;
    }

    if (::listen(server_fd, 64) < 0) {
        std::cerr << "listen() failed: " << std::strerror(errno) << "\n";
        ::close(server_fd);
        return 1;
    }

    std::cout << "Listening on http://127.0.0.1:" << port << "\n";

    while (true) {
        sockaddr_in client{};
        socklen_t len = sizeof(client);
        const int fd = ::accept(server_fd, reinterpret_cast<sockaddr*>(&client), &len);
        if (fd < 0) {
            std::cerr << "accept() failed: " << std::strerror(errno) << "\n";
            continue;
        }

        std::string raw;
        breeze::http::Response res;
        if (!read_http_request(fd, raw)) {
            res.set_status(400);
            res.set_body("Bad Request");
        } else {
            try {
                res = dispatch_http(app, raw);
            } catch (const std::exception& e) {
                res.set_status(500);
                res.set_body(std::string("Internal Server Error: ") + e.what());
            }
        }

        const auto out = serialize_http_response(res);
        (void)::send(fd, out.data(), out.size(), 0);
        ::close(fd);
    }
}

} // namespace

int main(int argc, char** argv)
{
    breeze::core::Application app;

    auto auth = [](const breeze::http::Request& req, breeze::http::MiddlewarePipeline::Next next) {
        if (req.header("X-Auth") != "1") {
            breeze::http::Response res;
            res.set_status(401);
            res.set_body("Unauthorized");
            return res;
        }
        return next(req);
    };

    breeze::http::Route::get("/", [](const breeze::http::Request&) {
        breeze::http::Response res;
        res.set_status(200);
        res.set_body("Welcome");
        return res;
    }).name("home");

    breeze::http::Route::prefix("/api").middleware(auth).group([] {
        breeze::http::Route::get("/ping", [](const breeze::http::Request&) {
            breeze::http::Response res;
            res.set_status(200);
            res.set_body("pong");
            return res;
        }).name("api.ping");
    });

    breeze::http::Request req;

    std::string method = "GET";
    std::string path = "/api/ping";
    std::string body;

    if (argc >= 2 && std::string(argv[1]) == "serve") {
        std::uint16_t port = 8080;
        if (argc >= 3) {
            port = static_cast<std::uint16_t>(std::stoi(argv[2]));
        }
        return serve(app, port);
    }

    if (argc >= 2) {
        method = argv[1];
    }
    if (argc >= 3) {
        path = argv[2];
    }

    for (int i = 3; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--body" && i + 1 < argc) {
            body = argv[++i];
            continue;
        }

        if (arg == "-H" && i + 1 < argc) {
            const std::string hv = argv[++i];
            const auto pos = hv.find('=');
            if (pos != std::string::npos) {
                req.set_header(hv.substr(0, pos), hv.substr(pos + 1));
            }
            continue;
        }

        const auto pos = arg.find('=');
        if (pos != std::string::npos) {
            req.set_header(arg.substr(0, pos), arg.substr(pos + 1));
        }
    }

    req.set_method(std::move(method));
    req.set_path(std::move(path));
    if (!body.empty()) {
        req.set_body(std::move(body));
    }

    const auto res = app.handle(req);
    std::cout << res.to_string() << "\n";
    return 0;
}
