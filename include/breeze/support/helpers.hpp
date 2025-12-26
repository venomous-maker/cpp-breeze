#pragma once
#include <breeze/core/application.hpp>
#include <breeze/support/env.hpp>
#include <string>

namespace breeze {

/**
 * Get an environment variable value.
 */
inline std::string env(const std::string& key, const std::string& fallback = "") {
    return support::Env::get(key, fallback);
}

/**
 * Get a configuration value.
 */
inline std::string config(const std::string& key, const std::string& fallback = "") {
    return core::Application::instance().config().get(key, fallback);
}

/**
 * Generate a URL for a named route.
 */
inline std::string route(const std::string& name, const std::unordered_map<std::string, std::string>& params = {}) {
    return core::Application::instance().kernel().router().route(name, params);
}

} // namespace breeze

// Global helpers (optional, but Laravel-like)
#ifndef BREEZE_NO_GLOBAL_HELPERS
using breeze::env;
using breeze::config;
using breeze::route;
#endif
