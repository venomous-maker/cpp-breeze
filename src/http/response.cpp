#include <breeze/http/response.hpp>

#include <sstream>

namespace breeze::http {

std::string Response::to_string() const
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << static_cast<int>(status_);

    std::string reason = Status::reason_phrase(status_);
    if (!reason.empty()) {
        oss << " " << reason;
    }
    oss << "\r\n";

    for (const auto& [k, v] : headers_) {
        oss << k << ": " << v << "\r\n";
    }
    oss << "\r\n" << body_;
    return oss.str();
}

} // namespace breeze::http
