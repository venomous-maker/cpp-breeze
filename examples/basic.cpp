#include <breeze/breeze.hpp>

#include <iostream>

int main()
{
    breeze::core::Application app;

    app.kernel().router().get("/", [](const breeze::http::Request& req) {
        (void)req;
        breeze::http::Response res;
        res.set_status(200);
        res.set_body("Hello from breeze");
        return res;
    });

    breeze::http::Request req;
    req.set_method("GET");
    req.set_path("/");

    const auto res = app.handle(req);
    std::cout << res.to_string() << "\n";
    return 0;
}
