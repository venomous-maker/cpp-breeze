# Breeze Framework

A Laravel-inspired C++ web framework designed for high performance, ease of use, and a familiar developer experience.

## Features

- **Laravel-Style Routing**: Supports groups, middleware, named routes, and controller method mapping.
- **Dependency Injection Container**: A powerful DI container for managing class dependencies and performing auto-wiring.
- **Middleware Support**: Easily intercept and modify requests and responses.
- **Configuration & Environment**: Load configuration from JSON files and manage environment variables via `.env` files.
- **View Engine**: A Laravel Blade-inspired view engine with support for variable parsing, `@if`, `@unless`, and `@foreach` directives.
- **Artisan-like CLI**: A built-in command-line interface for common tasks like serving the application.
- **Modern C++**: Built with C++20 for performance and safety.

## Project Structure

The project follows a Laravel-like directory structure:

```text
.
├── app/                # Application logic
│   ├── Http/           # HTTP layer
│   │   ├── Controllers/# Request controllers
│   │   └── Middleware/ # Custom middleware
│   ├── Models/         # Database models
│   └── Providers/      # Service providers
├── config/             # Configuration files (JSON)
├── database/           # Database migrations and seeders
├── include/            # Framework header files
├── public/             # Publicly accessible assets
├── resources/          # Resources like views (Blade-like)
├── routes/             # Route definitions (web.cpp, api.cpp)
├── src/                # Framework source code
├── storage/            # Logs, cache, and framework files
└── main.cpp            # Application entry point
```

## Getting Started

### Prerequisites

- CMake 3.20 or higher
- C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 2019+)
- [nlohmann/json](https://github.com/nlohmann/json) (automatically fetched by CMake)

### Installation & Build

1. Clone the repository:
   ```bash
   git clone https://github.com/your-repo/breeze.git
   cd breeze
   ```

2. Build the project:
   ```bash
   mkdir build && cd build
   cmake ..
   make
   ```

### Running the Application

You can use the Breeze CLI to serve the application:

```bash
./breeze_cli serve --port=8000
```

Or run the main executable directly:

```bash
./breeze_app 8000
```

## Usage Examples

### Defining Routes (`routes/web.cpp`)

```cpp
router.get("/", [](const Request& req) {
    return Response::ok("Hello Breeze!");
});

router.controller<UserController>("/users").group([](auto& group) {
    group.get("/", &UserController::index);
    group.get("/{id}", &UserController::show);
});
```

### Creating a Controller

```cpp
class UserController : public breeze::http::Controller {
public:
    breeze::http::Response index(const Request& req) {
        return Response::ok("User list");
    }
};
```

### Environment Variables

Create a `.env` file in the root directory:

```env
APP_NAME=MyBreezeApp
APP_ENV=local
APP_PORT=8000
```

Access them in your code:

```cpp
std::string name = breeze::support::Env::get("APP_NAME");
```

### Views and Templates

Breeze supports simple HTML templates with data binding. Place your templates in `resources/views`.

**Template (`resources/views/welcome.breeze`):**
```html
<h1>Hello, {{ name }}!</h1>
@if(show_features)
<ul>
    @foreach(features as feature)
        <li>{{ feature }}</li>
    @endforeach
</ul>
@endif
```

**Controller:**
```cpp
return Response::view("welcome", {
    {"name", "Developer"},
    {"show_features", true},
    {"features", {"Fast", "Easy", "C++"}}
});
```

## Template examples

Inline C++ example (opt-in, disabled by default):

- `resources/views/inline_cpp_example.breeze` demonstrates using `@cpp{ ... }` to include C++ code. Inline compilation is disabled by default. To enable, set the config key `view.inline_cpp.enabled` to `true` or set environment variable `BREEZE_INLINE_CPP=true` (not recommended in production).

Inline Breeze example:

- `resources/views/inline_breeze_example.breeze` demonstrates breeze template features: expressions, filters (`upper`, `default`), `@if`, and `@foreach`.

## Security and sandboxing

Compiling and executing inline C++ code can be dangerous. The project includes an example sandbox helper script `scripts/sandbox_run.sh` which demonstrates how to run a compiled binary with resource limits (CPU time and virtual memory) using `ulimit` and `timeout`.

This is a best-effort helper and is NOT a full sandbox. For production use, consider running compiled code inside containers, using seccomp profiles, or using specialized sandboxing tools.

Example usage (local dev):

```bash
# Enable inline C++ (dev only)
export BREEZE_INLINE_CPP=true
# Start the app
./breeze_app
```

## Admin routes

Two admin endpoints were added to inspect and clear the Blade view cache:

- GET /admin/blade/cache  — returns JSON with cache stats (hits, misses, entries, max_items, ttl_seconds).
- POST /admin/blade/clear — clears both in-memory and on-disk compiled view caches.

These routes should be protected in production; they are convenience endpoints for local development.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## License

The Breeze framework is open-source software licensed under the [MIT license](LICENSE).
