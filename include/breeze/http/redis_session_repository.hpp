#pragma once

#include "breeze/http/i_session_repository.hpp"
#include "breeze/http/session.hpp"

#include <string>
#include <memory>
#ifdef BREEZE_HAVE_HIREDIS
#include <hiredis/hiredis.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif
#include <sstream>
#include <vector>
#include <random>
#include <iomanip>

namespace breeze::http {

class Request; // forward

// Redis-backed session repository. When built with hiredis, use hiredis for
// reliable Redis operations; otherwise fall back to a minimal RESP-over-TCP
// implementation.
class RedisSessionRepository : public ISessionRepository {
public:
    explicit RedisSessionRepository(std::string host = "127.0.0.1", int port = 6379, std::string prefix = "breeze:session:")
        : host_(std::move(host)), port_(port), prefix_(std::move(prefix)) {
#ifdef BREEZE_HAVE_HIREDIS
        ctx_ = redisConnect(host_.c_str(), port_);
        if (!ctx_ || ctx_->err) {
            if (ctx_) redisFree(ctx_);
            ctx_ = nullptr;
        }
#endif
    }

    ~RedisSessionRepository() override {
#ifdef BREEZE_HAVE_HIREDIS
        if (ctx_) redisFree(ctx_);
#endif
    }

    std::tuple<std::string, std::shared_ptr<Session>, bool> get_session_for_request(const Request& req) override {
        std::string sid = req.cookie("BREEZE_SESSION", "");
        if (!sid.empty()) {
            auto s = load_session(sid);
            if (s) return {sid, s, false};
        }
        // create new
        std::string new_sid = generate_id();
        auto s = std::make_shared<Session>();
        return {new_sid, s, true};
    }

    [[nodiscard]] bool save(const std::string& session_id, const std::shared_ptr<Session>& session) override {
        std::string key = prefix_ + session_id;
        auto j = session->serialize();
        std::string val = j.dump();
#ifdef BREEZE_HAVE_HIREDIS
        if (!ctx_) return false;
        redisReply* reply = static_cast<redisReply*>(redisCommand(ctx_, "SET %b %b", key.data(), (size_t)key.size(), val.data(), (size_t)val.size()));
        if (!reply) return false;
        bool ok = (reply->type == REDIS_REPLY_STATUS && std::string(reply->str) == "OK");
        freeReplyObject(reply);
        return ok;
#else
        return redis_set(key, val);
#endif
    }

    [[nodiscard]] std::shared_ptr<Session> load_session(const std::string& session_id) override {
        std::string key = prefix_ + session_id;
        std::string val;
#ifdef BREEZE_HAVE_HIREDIS
        if (!ctx_) return nullptr;
        redisReply* reply = static_cast<redisReply*>(redisCommand(ctx_, "GET %b", key.data(), (size_t)key.size()));
        if (!reply) return nullptr;
        if (reply->type == REDIS_REPLY_NIL) { freeReplyObject(reply); return nullptr; }
        if (reply->type == REDIS_REPLY_STRING) {
            val.assign(reply->str, reply->len);
        }
        freeReplyObject(reply);
        if (val.empty()) return nullptr;
        try {
            auto j = nlohmann::json::parse(val);
            auto s = std::make_shared<Session>();
            s->deserialize(j);
            return s;
        } catch (...) {
            return nullptr;
        }
#else
        if (!redis_get(key, val)) return nullptr;
        try {
            auto j = nlohmann::json::parse(val);
            auto s = std::make_shared<Session>();
            s->deserialize(j);
            return s;
        } catch (...) {
            return nullptr;
        }
#endif
    }

private:
#ifndef BREEZE_HAVE_HIREDIS
    // Minimal fallback implementations (as before)
    bool redis_get(const std::string& key, std::string& out) {
        int fd = connect_socket();
        if (fd < 0) return false;
        std::string cmd = build_command({"GET", key});
        if (!send_all(fd, cmd)) { close(fd); return false; }
        std::string resp;
        if (!recv_all(fd, resp)) { close(fd); return false; }
        close(fd);
        // parse simple bulk reply $len\r\n<data>\r\n or Null bulk reply $-1
        if (!resp.empty() && resp[0] == '$') {
            if (resp.find("$-1") == 0) return false;
            size_t pos = resp.find("\r\n");
            if (pos == std::string::npos) return false;
            size_t len = std::stoul(resp.substr(1, pos-1));
            out = resp.substr(pos + 2, len);
            return true;
        }
        return false;
    }

    bool redis_set(const std::string& key, const std::string& val) {
        int fd = connect_socket();
        if (fd < 0) return false;
        std::string cmd = build_command({"SET", key, val});
        if (!send_all(fd, cmd)) { close(fd); return false; }
        std::string resp;
        if (!recv_all(fd, resp)) { close(fd); return false; }
        close(fd);
        // Expect simple string reply "+OK\r\n"
        return resp.size() >= 4 && resp[0] == '+';
    }

    [[nodiscard]] int connect_socket() const {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        if (inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0) { close(fd); return -1; }
        if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) { close(fd); return -1; }
        return fd;
    }

    static std::string build_command(const std::vector<std::string>& parts) {
        std::ostringstream ss;
        ss << '*' << parts.size() << "\r\n";
        for (const auto& p : parts) {
            ss << '$' << p.size() << "\r\n" << p << "\r\n";
        }
        return ss.str();
    }

    static bool send_all(int fd, const std::string& data) {
        size_t sent = 0;
        const char* ptr = data.data();
        while (sent < data.size()) {
            ssize_t n = ::send(fd, ptr + sent, data.size() - sent, 0);
            if (n <= 0) return false;
            sent += n;
        }
        return true;
    }

    static bool recv_all(int fd, std::string& out) {
        char buf[4096];
        out.clear();
        ssize_t n;
        // Read until socket closes or we find \r\n (for simple replies). We'll read up to some bytes.
        while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
            out.append(buf, buf + n);
            // For simple protocol parsing, break early if we got CRLF
            if (out.find("\r\n") != std::string::npos) break;
        }
        return n >= 0;
    }
#endif

    [[nodiscard]] static std::string generate_id() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;
        uint64_t a = dis(gen);
        uint64_t b = dis(gen);
        std::ostringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << a << std::setw(16) << b;
        return ss.str();
    }

#ifdef BREEZE_HAVE_HIREDIS
    redisContext* ctx_ = nullptr;
#endif
    std::string host_;
    int port_ = 6379;
    std::string prefix_;
};

} // namespace breeze::http

