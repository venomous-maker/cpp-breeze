#pragma once

#include <nlohmann/json.hpp>
#include <unordered_map>
#include <string>
#include <mutex>

namespace breeze::http {

// Minimal Laravel-like Session implementation built on a simple in-memory
// store. This is intentionally synchronous and in-memory for the example.
// An application may replace this with a persistent/session-per-user store.
class Session {
public:
    Session() = default;

    // Get value, or fallback
    nlohmann::json get(const std::string& key, nlohmann::json fallback = {}) const {
        std::lock_guard<std::mutex> lk(m_);
        auto it = data_.find(key);
        if (it == data_.end()) return fallback;
        return it->second;
    }

    // Put/overwrite value
    void put(const std::string& key, const nlohmann::json& value) {
        std::lock_guard<std::mutex> lk(m_);
        data_[key] = value;
    }

    bool has(const std::string& key) const {
        std::lock_guard<std::mutex> lk(m_);
        return data_.find(key) != data_.end();
    }

    void forget(const std::string& key) {
        std::lock_guard<std::mutex> lk(m_);
        data_.erase(key);
    }

    // Pull: get and remove
    nlohmann::json pull(const std::string& key, nlohmann::json fallback = {}) {
        std::lock_guard<std::mutex> lk(m_);
        auto it = data_.find(key);
        if (it == data_.end()) return fallback;
        auto val = it->second;
        data_.erase(it);
        return val;
    }

    // Flash data for next request
    void flash(const std::string& key, const nlohmann::json& value) {
        std::lock_guard<std::mutex> lk(m_);
        flash_next_[key] = value;
    }

    // Retrieve flash data available for this request (set from previous request)
    nlohmann::json flash_now(const std::string& key, nlohmann::json fallback = {}) const {
        std::lock_guard<std::mutex> lk(m_);
        auto it = flash_now_.find(key);
        if (it == flash_now_.end()) return fallback;
        return it->second;
    }

    // Keep an item from flash_now into next request
    void keep(const std::string& key) {
        std::lock_guard<std::mutex> lk(m_);
        auto it = flash_now_.find(key);
        if (it != flash_now_.end()) flash_next_[key] = it->second;
    }

    // Reflash all current flash_now items into next request
    void reflash() {
        std::lock_guard<std::mutex> lk(m_);
        for (auto &p : flash_now_) flash_next_[p.first] = p.second;
    }

    // Move flash_next into flash_now; clear flash_next. Call at request start.
    void sweep_flash() {
        std::lock_guard<std::mutex> lk(m_);
        flash_now_.clear();
        for (auto &p : flash_next_) flash_now_[p.first] = p.second;
        flash_next_.clear();
    }

    // Get and clear all session data
    nlohmann::json all() const {
        std::lock_guard<std::mutex> lk(m_);
        nlohmann::json out = nlohmann::json::object();
        for (auto &p : data_) out[p.first] = p.second;
        return out;
    }

    void clear() {
        std::lock_guard<std::mutex> lk(m_);
        data_.clear();
        flash_now_.clear();
        flash_next_.clear();
    }

private:
    mutable std::mutex m_;
    std::unordered_map<std::string, nlohmann::json> data_;
    // flash semantics
    std::unordered_map<std::string, nlohmann::json> flash_next_;
    std::unordered_map<std::string, nlohmann::json> flash_now_;
};

} // namespace breeze::http

