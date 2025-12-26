#pragma once
#include <breeze/http/request.hpp>
#include <breeze/http/response.hpp>

namespace breeze::http {

class Controller {
public:
    virtual ~Controller() = default;
};

} // namespace breeze::http
