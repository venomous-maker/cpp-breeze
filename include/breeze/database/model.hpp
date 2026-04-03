#pragma once

#include <breeze/database/query.hpp>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <optional>
#include <functional>

namespace breeze::database {

class Model {
public:
    Model() = default;
    virtual ~Model() = default;

    // Copy and move
    Model(const Model&) = default;
    Model& operator=(const Model&) = default;
    Model(Model&&) noexcept = default;
    Model& operator=(Model&&) noexcept = default;

    // Attribute access
    void set(std::string key, std::string value) { attributes_[std::move(key)] = std::move(value); }

    const std::string* get(const std::string& key) const
    {
        auto it = attributes_.find(key);
        if (it == attributes_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::string getOr(const std::string& key, const std::string& defaultValue) const
    {
        auto it = attributes_.find(key);
        if (it == attributes_.end()) {
            return defaultValue;
        }
        return it->second;
    }

    bool has(const std::string& key) const
    {
        return attributes_.find(key) != attributes_.end();
    }

    void remove(const std::string& key)
    {
        attributes_.erase(key);
    }

    void clear()
    {
        attributes_.clear();
    }

    // Operator access
    std::string& operator[](const std::string& key) { return attributes_[key]; }

    const std::string& operator[](const std::string& key) const
    {
        static std::string empty;
        auto it = attributes_.find(key);
        return it != attributes_.end() ? it->second : empty;
    }

    // Iterator support for accessing all attributes
    using AttributeMap = std::unordered_map<std::string, std::string>;

    const AttributeMap& attributes() const { return attributes_; }
    AttributeMap& attributes() { return attributes_; }

    AttributeMap::const_iterator begin() const { return attributes_.begin(); }
    AttributeMap::const_iterator end() const { return attributes_.end(); }
    AttributeMap::iterator begin() { return attributes_.begin(); }
    AttributeMap::iterator end() { return attributes_.end(); }

    bool empty() const { return attributes_.empty(); }
    size_t size() const { return attributes_.size(); }

    // Get all keys
    std::vector<std::string> keys() const
    {
        std::vector<std::string> result;
        result.reserve(attributes_.size());
        for (const auto& [key, _] : attributes_) {
            result.push_back(key);
        }
        return result;
    }

    // Get all values
    std::vector<std::string> values() const
    {
        std::vector<std::string> result;
        result.reserve(attributes_.size());
        for (const auto& [_, value] : attributes_) {
            result.push_back(value);
        }
        return result;
    }

    // Fill from key-value pairs
    void fill(const std::vector<std::pair<std::string, std::string>>& data)
    {
        for (const auto& [key, value] : data) {
            attributes_[key] = value;
        }
    }

    // Fill from another model (merge)
    void merge(const Model& other)
    {
        for (const auto& [key, value] : other) {
            attributes_[key] = value;
        }
    }

    // Only keep specified keys
    Model only(const std::vector<std::string>& keys) const
    {
        Model result;
        for (const auto& key : keys) {
            auto it = attributes_.find(key);
            if (it != attributes_.end()) {
                result.set(key, it->second);
            }
        }
        return result;
    }

    // Exclude specified keys
    Model except(const std::vector<std::string>& keys) const
    {
        Model result;
        for (const auto& [key, value] : attributes_) {
            bool excluded = false;
            for (const auto& excl : keys) {
                if (key == excl) {
                    excluded = true;
                    break;
                }
            }
            if (!excluded) {
                result.set(key, value);
            }
        }
        return result;
    }

    // Convert to vector of pairs (useful for SQL generation)
    std::vector<std::pair<std::string, std::string>> toVector() const
    {
        std::vector<std::pair<std::string, std::string>> result;
        result.reserve(attributes_.size());
        for (const auto& [key, value] : attributes_) {
            result.emplace_back(key, value);
        }
        return result;
    }

    // Primary key support
    void setPrimaryKey(const std::string& key) { primaryKey_ = key; }
    const std::string& getPrimaryKey() const { return primaryKey_; }

    std::optional<std::string> getId() const
    {
        auto it = attributes_.find(primaryKey_);
        if (it != attributes_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // Table name support (for derived models)
    void setTable(const std::string& table) { table_ = table; }
    const std::string& getTable() const { return table_; }

    // Create a query builder for this model's table
    static Query query(std::string table)
    {
        Query q;
        q.table(std::move(table));
        return q;
    }

    Query newQuery() const
    {
        Query q;
        q.table(table_);
        return q;
    }

    // Timestamps support
    void setCreatedAt(const std::string& timestamp) { attributes_["created_at"] = timestamp; }
    void setUpdatedAt(const std::string& timestamp) { attributes_["updated_at"] = timestamp; }

    std::optional<std::string> getCreatedAt() const
    {
        auto it = attributes_.find("created_at");
        if (it != attributes_.end()) return it->second;
        return std::nullopt;
    }

    std::optional<std::string> getUpdatedAt() const
    {
        auto it = attributes_.find("updated_at");
        if (it != attributes_.end()) return it->second;
        return std::nullopt;
    }

    // Dirty tracking
    void markClean() { original_ = attributes_; }

    bool isDirty() const { return attributes_ != original_; }

    bool isDirty(const std::string& key) const
    {
        auto curr = attributes_.find(key);
        auto orig = original_.find(key);
        if (curr == attributes_.end() && orig == original_.end()) return false;
        if (curr == attributes_.end() || orig == original_.end()) return true;
        return curr->second != orig->second;
    }

    std::vector<std::string> getDirty() const
    {
        std::vector<std::string> dirty;
        for (const auto& [key, value] : attributes_) {
            auto orig = original_.find(key);
            if (orig == original_.end() || orig->second != value) {
                dirty.push_back(key);
            }
        }
        return dirty;
    }

    // Get only changed attributes
    Model getChanges() const
    {
        Model changes;
        for (const auto& [key, value] : attributes_) {
            auto orig = original_.find(key);
            if (orig == original_.end() || orig->second != value) {
                changes.set(key, value);
            }
        }
        return changes;
    }

protected:
    std::unordered_map<std::string, std::string> attributes_;
    std::unordered_map<std::string, std::string> original_;
    std::string primaryKey_ = "id";
    std::string table_;
};

} // namespace breeze::database
