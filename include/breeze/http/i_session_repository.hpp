#pragma once

#include <memory>
#include <string>
#include <tuple>

namespace breeze::http {

class Request;
class Session;

// Pluggable session repository interface.
struct ISessionRepository {
    virtual ~ISessionRepository() = default;

    // Return tuple(session_id, session_ptr, is_new)
    virtual std::tuple<std::string, std::shared_ptr<Session>, bool> get_session_for_request(const Request& req) = 0;

    virtual bool save(const std::string& session_id, const std::shared_ptr<Session>& session) = 0;

    virtual std::shared_ptr<Session> load_session(const std::string& session_id) = 0;
};

} // namespace breeze::http

