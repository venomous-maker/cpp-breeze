// include/breeze/core/container.hpp
#pragma once
#include <any>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace breeze::core {

class Container {
public:
    // Basic binding
    template <class T>
    void bind(std::function<std::shared_ptr<T>()> factory) {
        auto key = std::type_index(typeid(T));
        bindings_[key] = [factory]() -> std::shared_ptr<void> {
            return std::static_pointer_cast<void>(factory());
        };
    }
    
    // Singleton binding
    template <class T>
    void singleton(std::function<std::shared_ptr<T>()> factory) {
        auto key = std::type_index(typeid(T));
        auto instance = factory();
        singletons_[key] = std::static_pointer_cast<void>(instance);
        singleton_factories_[key] = [instance]() {
            return std::static_pointer_cast<void>(instance);
        };
    }
    
    // Instance singleton
    template <class T>
    void singleton(std::shared_ptr<T> instance) {
        auto key = std::type_index(typeid(T));
        singletons_[key] = std::static_pointer_cast<void>(instance);
    }
    
    // Make with auto-wiring (simplified)
    template <class T>
    std::shared_ptr<T> make() {
        auto key = std::type_index(typeid(T));
        
        // Check singleton cache
        auto singleton_it = singletons_.find(key);
        if (singleton_it != singletons_.end()) {
            return std::static_pointer_cast<T>(singleton_it->second);
        }
        
        // Check singleton factory
        auto singleton_factory_it = singleton_factories_.find(key);
        if (singleton_factory_it != singleton_factories_.end()) {
            auto instance = std::static_pointer_cast<T>(singleton_factory_it->second());
            singletons_[key] = instance; // Cache it
            return instance;
        }
        
        // Check regular factory
        auto factory_it = bindings_.find(key);
        if (factory_it != bindings_.end()) {
            return std::static_pointer_cast<T>(factory_it->second());
        }
        
        // Auto-resolve with default constructor
        if constexpr (std::is_constructible_v<T>) {
            auto instance = std::make_shared<T>();
            return instance;
        } else {
            throw std::runtime_error("Cannot resolve type: " + std::string(typeid(T).name()));
        }
    }
    
    // Make with parameters
    template <class T, typename... Args>
    std::shared_ptr<T> make_with(Args&&... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
    
    // Check if can make
    template <class T>
    bool can_make() const {
        auto key = std::type_index(typeid(T));
        return bindings_.contains(key) || 
               singletons_.contains(key) ||
               singleton_factories_.contains(key) ||
               std::is_constructible_v<T>;
    }
    
    // Tagging system (for collecting services)
    template <class T, class Tag>
    void tag(std::shared_ptr<T> instance) {
        tagged_services_[typeid(Tag).hash_code()].push_back(instance);
    }
    
    template <class Tag>
    std::vector<std::shared_ptr<void>> tagged() {
        auto it = tagged_services_.find(typeid(Tag).hash_code());
        if (it != tagged_services_.end()) {
            return it->second;
        }
        return {};
    }
    
    // Instance access (alternative to make for already created instances)
    template <class T>
    std::shared_ptr<T> instance() {
        auto key = std::type_index(typeid(T));
        auto it = singletons_.find(key);
        if (it != singletons_.end()) {
            return std::static_pointer_cast<T>(it->second);
        }
        throw std::runtime_error("No singleton instance registered for type");
    }
    
    // Check if has instance
    template <class T>
    bool has() const {
        auto key = std::type_index(typeid(T));
        return singletons_.contains(key) || singleton_factories_.contains(key);
    }

private:
    std::unordered_map<std::type_index, 
        std::function<std::shared_ptr<void>()>> bindings_;
    std::unordered_map<std::type_index, 
        std::function<std::shared_ptr<void>()>> singleton_factories_;
    std::unordered_map<std::type_index, 
        std::shared_ptr<void>> singletons_;
    std::unordered_map<size_t, 
        std::vector<std::shared_ptr<void>>> tagged_services_;
};

} // namespace breeze::core