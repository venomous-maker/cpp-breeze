#pragma once

#include <type_traits>
#include <utility>
#include <vector>

namespace breeze::support {

template <class T>
class Collection {
public:
    using value_type = T;

    Collection() = default;

    explicit Collection(std::vector<T> items) : items_(std::move(items)) {}

    void push(T value) { items_.push_back(std::move(value)); }

    std::size_t size() const { return items_.size(); }

    const std::vector<T>& items() const { return items_; }

    auto begin() { return items_.begin(); }
    auto end() { return items_.end(); }
    auto begin() const { return items_.begin(); }
    auto end() const { return items_.end(); }

    template <class Func>
    auto map(Func&& f) const -> Collection<std::invoke_result_t<Func, const T&>>
    {
        using U = std::invoke_result_t<Func, const T&>;
        std::vector<U> out;
        out.reserve(items_.size());
        for (const auto& item : items_) {
            out.push_back(f(item));
        }
        return Collection<U>(std::move(out));
    }

private:
    std::vector<T> items_;
};

} // namespace breeze::support
