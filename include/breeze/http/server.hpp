#pragma once

#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>
#include <breeze/http/status_code.hpp>
#include <breeze/http/session.hpp>
#include <breeze/http/parsers.hpp>
#include <breeze/http/session_store.hpp>
#include "breeze/http/socket_stream.hpp"
#include "breeze/http/multipart_stream_parser.hpp"
#include "breeze/http/chunked_stream.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <stdexcept>
#include <ctime>

namespace breeze::http {

/**
 * Basic HTTP Server implementation for Breeze.
 * Handles socket-level communication, basic HTTP parsing, and multi-threaded request dispatching.
 */
class Server {
public:
    using RequestHandler = std::function<Response(const Request&)>;

    explicit Server(RequestHandler handler, std::shared_ptr<ISessionRepository> repo = nullptr, std::string upload_dir = "/tmp",
                    size_t max_request_size = 0, size_t max_file_size = 0,
                    std::vector<std::string> allowed_exts = {}, std::vector<std::string> allowed_types = {},
                    std::function<bool(const std::string&, const UploadedFile&)> scan_cb = nullptr)
        : handler_(std::move(handler)), repo_(std::move(repo)), upload_dir_(std::move(upload_dir)),
          max_request_size_(max_request_size), max_file_size_(max_file_size), allowed_exts_(std::move(allowed_exts)), allowed_types_(std::move(allowed_types)), scan_cb_(std::move(scan_cb)) {
        if (!repo_) repo_ = std::make_shared<FileSessionRepository>();
    }

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

        // server loop

        while (true) {
            sockaddr_in client_address{};
            socklen_t client_len = sizeof(client_address);
            int client_fd = accept(server_fd, (struct sockaddr*)&client_address, &client_len);
            if (client_fd < 0) {
                continue;
            }

            // Capture client_address by value and pass it to the handler thread
            std::thread([this, client_fd, client_address]() mutable {
                handle_client(client_fd, client_address);
            }).detach();
        }
    }

private:
    // Updated to accept client address so we can inject remote IP into the Request headers
    void handle_client(int client_fd, const sockaddr_in& client_address) {
        // create a socket-backed istream
        breeze::http::socket_istream in(client_fd);

        // parse request line and headers from the stream
        Request req;
        Response early_error;
        bool early_error_set = false;
        try {
            req = parse_request_from_stream(in, early_error, early_error_set);
        } catch (...) {
            // parsing fatal error
            early_error = Response::bad_request("Malformed request");
            early_error_set = true;
        }

        if (early_error_set) {
            // send error immediately
            std::string raw_response = early_error.to_string();
            send(client_fd, raw_response.c_str(), raw_response.size(), 0);
            close(client_fd);
            return;
        }

        // Resolve per-client session using repository (based on cookie)
        std::string session_id;
        bool session_is_new = false;
        std::shared_ptr<Session> session_ptr;
        std::tie(session_id, session_ptr, session_is_new) = repo_->get_session_for_request(req);
        req.set_session(session_ptr);

        // Inject remote IP into headers so middlewares/controllers can read client IP
        char ipbuf[INET_ADDRSTRLEN] = {0};
        const char* ip = inet_ntop(AF_INET, &client_address.sin_addr, ipbuf, sizeof(ipbuf));
        if (ip) {
            req.set_header("x-remote-addr", std::string(ip));
        } else {
            req.set_header("x-remote-addr", std::string("unknown"));
        }

        Response res = handler_(req);

        // If we created a new session, set cookie on response
        if (session_is_new) {
            res.with_cookie("BREEZE_SESSION", session_id, 60*60*24*30, "/", true, false);
        }

        // Persist session
        repo_->save(session_id, session_ptr);

        // Add CPP BREEZE SIGNATURE
        if (res.header("X-Powered-By", "").empty()) {
            res.with_header("X-Powered-By", "Cpp Breeze");
        }
        std::string raw_response = res.to_string();
        send(client_fd, raw_response.c_str(), raw_response.size(), 0);
        close(client_fd);
    }

    // parse_request_from_stream may set an early error response (via early_error)
    // and return an empty Request; early_error_set indicates an immediate response should be sent.
    Request parse_request_from_stream(std::istream& in, Response& early_error, bool& early_error_set) {
        Request req;
        std::string line;
        // Request line
        if (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::istringstream line_stream(line);
            std::string method, path, version;
            line_stream >> method >> path >> version;
            req.set_method(method);

            size_t query_pos = path.find('?');
            if (query_pos != std::string::npos) {
                std::string path_only = path.substr(0, query_pos);
                if (path_only.size() > 1 && path_only.back() == '/') {
                    path_only.pop_back();
                }
                req.set_path(path_only);
                req.set_query_string(path.substr(query_pos + 1));
            } else {
                std::string path_only = path;
                if (path_only.size() > 1 && path_only.back() == '/') {
                    path_only.pop_back();
                }
                req.set_path(path_only);
            }
        }

        // Headers
        while (std::getline(in, line) && line != "\r" && !line.empty()) {
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

        // detect transfer-encoding: chunked
        std::string te = req.header("transfer-encoding", "");
        bool is_chunked = false;
        std::string lte = te; std::transform(lte.begin(), lte.end(), lte.begin(), ::tolower);
        if (lte.find("chunked") != std::string::npos) is_chunked = true;

        std::istream* body_stream_ptr = &in; // default uses socket stream
        std::unique_ptr<breeze::http::chunked_istream> chunked_in;
        if (is_chunked) {
            // use chunked_istream to decode on-the-fly (zero temp files)
            chunked_in = std::make_unique<breeze::http::chunked_istream>(in);
            body_stream_ptr = chunked_in.get();
        }

        // After headers, decide how to handle body. If multipart, stream it using MultipartStreamParser.
        std::string ct = req.header("content-type", "");
        std::string lct = ct;
        std::transform(lct.begin(), lct.end(), lct.begin(), ::tolower);
        if (lct.find("multipart/form-data") != std::string::npos) {
            // extract boundary
            size_t bpos = ct.find("boundary=");
            if (bpos != std::string::npos) {
                std::string boundary = ct.substr(bpos + 9);
                if (!boundary.empty() && boundary.front() == '"' && boundary.back() == '"') boundary = boundary.substr(1, boundary.size()-2);
                // Use MultipartStreamParser to stream parts. It will read from 'in' directly and write files to disk.
                std::unordered_map<std::string, std::string> fields;
                std::unordered_map<std::string, UploadedFile> files;
                // Pass configured limits from config if available
                MultipartStreamParser parser(*body_stream_ptr, boundary, upload_dir_, max_request_size_, max_file_size_, allowed_exts_, allowed_types_, scan_cb_);
                auto result = parser.parse(
                    [&](const std::string& name, const std::string& value){ fields[name] = value; },
                    [&](const std::string& name, const UploadedFile& uf){ files[name] = uf; }
                );
                if (result != MultipartStreamParser::Result::Ok) {
                    switch (result) {
                        case MultipartStreamParser::Result::RequestTooLarge:
                        case MultipartStreamParser::Result::FileTooLarge:
                            early_error = Response::bad_request("Request entity too large");
                            early_error.set_status(413);
                            break;
                        case MultipartStreamParser::Result::InvalidExtension:
                        case MultipartStreamParser::Result::InvalidContentType:
                            early_error = Response::bad_request("Unsupported media type");
                            early_error.set_status(415);
                            break;
                        case MultipartStreamParser::Result::ScanRejected:
                            early_error = Response::bad_request("File rejected by scanner");
                            early_error.set_status(422);
                            break;
                        default:
                            early_error = Response::bad_request("Invalid multipart data");
                            break;
                    }
                    early_error_set = true;
                    return req;
                }
                req.set_form_params(fields);
                req.set_uploaded_files(files);
            }
        } else {
            // Non-multipart: read Content-Length if present and read exact bytes
            auto clstr = req.header("content-length", "");
            if (!clstr.empty()) {
                size_t content_length = std::stoul(clstr);
                std::string body;
                body.reserve(content_length);
                char c;
                std::istream& src = *body_stream_ptr;
                for (size_t i = 0; i < content_length; ++i) {
                    if (!src.get(c)) break;
                    body.push_back(c);
                }
                req.set_body(body);
                if (lct.find("application/x-www-form-urlencoded") != std::string::npos) {
                    auto m = Parsers::parse_form_urlencoded(body);
                    req.set_form_params(m);
                }
            }
        }


        return req;
    }


    static void trim_crlf(std::string& s) {
        if (!s.empty() && s.back() == '\r') s.pop_back();
    }

    RequestHandler handler_;
    std::shared_ptr<ISessionRepository> repo_;
    std::string upload_dir_;
    size_t max_request_size_;
    size_t max_file_size_;
    std::vector<std::string> allowed_exts_;
    std::vector<std::string> allowed_types_;
    std::function<bool(const std::string&, const UploadedFile&)> scan_cb_;
};

} // namespace breeze::http
