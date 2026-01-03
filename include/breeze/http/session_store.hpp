#pragma once

#include "breeze/http/session.hpp"
#include "breeze/http/i_session_repository.hpp"
#include <string>
#include <memory>
#include <fstream>
#include <random>
#include <sstream>
#include <iomanip>
#include <sys/stat.h>
#include <unistd.h>

namespace breeze::http {

class Request; // forward

// File-based session repository implementing ISessionRepository.
class FileSessionRepository : public ISessionRepository {
public:
    explicit FileSessionRepository(std::string dir = "/tmp/breeze_sessions") : dir_(std::move(dir)) {
        // create dir if not exists
        struct stat st{};
        if (stat(dir_.c_str(), &st) != 0) {
            mkdir(dir_.c_str(), 0700);
        }
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

    bool save(const std::string& session_id, const std::shared_ptr<Session>& session) override {
        std::string path = path_for(session_id);
        nlohmann::json j = session->serialize();
        std::ofstream ofs(path, std::ios::trunc);
        if (!ofs) return false;
        ofs << j.dump();
        return true;
    }

    std::shared_ptr<Session> load_session(const std::string& session_id) override {
        std::string path = path_for(session_id);
        std::ifstream ifs(path);
        if (!ifs) return nullptr;
        std::stringstream ss; ss << ifs.rdbuf();
        try {
            auto j = nlohmann::json::parse(ss.str());
            auto s = std::make_shared<Session>();
            s->deserialize(j);
            return s;
        } catch (...) {
            return nullptr;
        }
    }

private:
    [[nodiscard]] std::string path_for(const std::string& session_id) const {
        return dir_ + "/session_" + session_id + ".json";
    }

    static std::string generate_id() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;
        uint64_t a = dis(gen);
        uint64_t b = dis(gen);
        std::ostringstream ss;
        ss << std::hex << std::setw(16) << std::setfill('0') << a << std::setw(16) << b;
        return ss.str();
    }

    std::string dir_;
};

} // namespace breeze::http

