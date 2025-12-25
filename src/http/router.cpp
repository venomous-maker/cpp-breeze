#include <breeze/http/router.hpp>

#include <algorithm>
#include <utility>

namespace breeze::http {

Router::RouteDefinition& Router::RouteDefinition::name(std::string name)
{
    if (router_ == nullptr) {
        throw std::logic_error("RouteDefinition is not bound to a router");
    }
    auto it = router_->routes_.find(key_);
    if (it == router_->routes_.end()) {
        throw std::logic_error("RouteDefinition refers to missing route");
    }
    it->second.name = std::move(name);
    return *this;
}

Router::RouteDefinition& Router::RouteDefinition::middleware(Middleware mw)
{
    if (router_ == nullptr) {
        throw std::logic_error("RouteDefinition is not bound to a router");
    }
    auto it = router_->routes_.find(key_);
    if (it == router_->routes_.end()) {
        throw std::logic_error("RouteDefinition refers to missing route");
    }
    it->second.middlewares.push_back(std::move(mw));
    return *this;
}

std::string Router::make_key(const std::string& method, const std::string& path)
{
    return method + " " + path;
}

Router::RouteDefinition Router::add(std::string method, std::string path, Handler handler)
{
    auto key = make_key(method, path);
    routes_[key] = RouteEntry{.handler = std::move(handler)};
    return RouteDefinition(this, std::move(key));
}

Response Router::dispatch(const Request& request) const
{
    auto it = routes_.find(make_key(request.method(), request.path()));
    if (it == routes_.end()) {
        Response res;
        res.set_status(404);
        res.set_body("Not Found");
        return res;
    }

    const auto& entry = it->second;
    if (entry.middlewares.empty()) {
        return entry.handler(request);
    }

    MiddlewarePipeline pipeline;
    for (const auto& mw : entry.middlewares) {
        pipeline.add(mw);
    }
    return pipeline.run(request, entry.handler);
}

Router& Route::router()
{
    if (router_ == nullptr) {
        throw std::logic_error("Route router is not set");
    }
    return *router_;
}

std::string Route::normalize_prefix(std::string prefix)
{
    if (prefix.empty()) {
        return {};
    }
    if (prefix.front() != '/') {
        prefix.insert(prefix.begin(), '/');
    }
    while (prefix.size() > 1 && prefix.back() == '/') {
        prefix.pop_back();
    }
    return prefix;
}

std::string Route::normalize_path(std::string path)
{
    if (path.empty()) {
        return "/";
    }
    if (path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    if (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

std::string Route::join_paths(const std::string& prefix, const std::string& path)
{
    const auto pfx = normalize_prefix(prefix);
    const auto pth = normalize_path(path);

    if (pfx.empty() || pfx == "/") {
        return pth;
    }
    if (pth == "/") {
        return pfx;
    }
    return pfx + pth;
}

std::string Route::apply_context_prefix(std::string path)
{
    std::string prefix;
    for (const auto& ctx : contexts_) {
        prefix = join_paths(prefix, ctx.prefix);
    }
    return join_paths(prefix, std::move(path));
}

std::vector<Route::Middleware> Route::gather_context_middlewares()
{
    std::vector<Middleware> result;
    for (const auto& ctx : contexts_) {
        result.insert(result.end(), ctx.middlewares.begin(), ctx.middlewares.end());
    }
    return result;
}

Router::RouteDefinition Route::add(std::string method, std::string path, Handler handler)
{
    path = apply_context_prefix(std::move(path));
    auto def = router().add(std::move(method), std::move(path), std::move(handler));
    for (auto& mw : gather_context_middlewares()) {
        def.middleware(std::move(mw));
    }
    return def;
}

Router::RouteDefinition Route::get(std::string path, Handler handler)
{
    return add("GET", std::move(path), std::move(handler));
}

Router::RouteDefinition Route::post(std::string path, Handler handler)
{
    return add("POST", std::move(path), std::move(handler));
}

Route::Group Route::Group::prefix(std::string prefix) const
{
    return Group(join_paths(prefix_, std::move(prefix)), middlewares_);
}

Route::Group Route::Group::middleware(Middleware mw) const
{
    auto mws = middlewares_;
    mws.push_back(std::move(mw));
    return Group(prefix_, std::move(mws));
}

Route::Group::ScopedGroup::ScopedGroup(const Group& group)
{
    Route::contexts_.push_back(Context{.prefix = group.prefix_, .middlewares = group.middlewares_});
    active_ = true;
}

Route::Group::ScopedGroup::~ScopedGroup()
{
    if (!active_) {
        return;
    }
    Route::contexts_.pop_back();
}

} // namespace breeze::http
