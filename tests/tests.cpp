#include <breeze/breeze.hpp>

#include <cassert>

int main()
{
    breeze::core::Application app;

    app.kernel().router().get("/ping", [](const breeze::http::Request&) {
        breeze::http::Response res;
        res.set_status(200);
        res.set_body("pong");
        return res;
    });

    breeze::http::Request req;
    req.set_method("GET");
    req.set_path("/ping");

    const auto res = app.handle(req);
    assert(res.status() == 200);
    assert(res.body() == "pong");
    return 0;
}
