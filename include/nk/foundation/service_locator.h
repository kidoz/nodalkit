#pragma once

/// @file service_locator.h
/// @brief Lightweight Dependency Injection container for application-level services.

#include <any>
#include <memory>
#include <mutex>
#include <typeindex>
#include <unordered_map>

namespace nk {

/// Global service registry allowing decoupled access to application services.
/// Typically used to provide implementations of abstract interfaces (like network,
/// database, or system services), and allows injecting mocks during tests.
class ServiceLocator {
public:
    /// Registers an implementation instance against an interface type.
    template <typename Interface, typename Implementation>
    static void register_service(std::shared_ptr<Implementation> instance) {
        std::shared_ptr<Interface> interface_ptr = instance;
        std::any displaced(std::move(interface_ptr));
        {
            std::lock_guard<std::mutex> lock(mutex());
            services()[std::type_index(typeid(Interface))].swap(displaced);
        }
    }

    /// Registers a newly constructed instance of the given type.
    template <typename Type> static void register_service() {
        register_service<Type, Type>(std::make_shared<Type>());
    }

    /// Returns the registered service, or an empty pointer when it is missing
    /// or the stored type does not match. Does not throw.
    template <typename Interface> [[nodiscard]] static std::shared_ptr<Interface> get() {
        std::lock_guard<std::mutex> lock(mutex());
        auto& s = services();
        auto it = s.find(std::type_index(typeid(Interface)));
        if (it == s.end()) {
            return {};
        }
        const auto* stored = std::any_cast<std::shared_ptr<Interface>>(&it->second);
        return stored != nullptr ? *stored : std::shared_ptr<Interface>{};
    }

    /// Checks if a service is currently registered.
    template <typename Interface> [[nodiscard]] static bool has() {
        std::lock_guard<std::mutex> lock(mutex());
        auto& s = services();
        return s.find(std::type_index(typeid(Interface))) != s.end();
    }

    /// Clears all registered services. Useful between tests.
    static void clear() {
        std::unordered_map<std::type_index, std::any> displaced;
        {
            std::lock_guard<std::mutex> lock(mutex());
            displaced.swap(services());
        }
    }

private:
    [[nodiscard]] static std::unordered_map<std::type_index, std::any>& services();
    [[nodiscard]] static std::mutex& mutex();
};

} // namespace nk
