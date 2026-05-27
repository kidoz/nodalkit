#pragma once

/// @file service_locator.h
/// @brief Lightweight Dependency Injection container for application-level services.

#include <any>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
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
        std::lock_guard<std::mutex> lock(mutex());
        std::shared_ptr<Interface> interface_ptr = instance;
        services()[std::type_index(typeid(Interface))] = std::move(interface_ptr);
    }

    /// Registers a newly constructed instance of the given type.
    template <typename Type>
    static void register_service() {
        register_service<Type, Type>(std::make_shared<Type>());
    }

    /// Retrieves the registered service of the requested type.
    /// @throws std::runtime_error if the service is not found.
    template <typename Interface>
    [[nodiscard]] static std::shared_ptr<Interface> get() {
        std::lock_guard<std::mutex> lock(mutex());
        auto& s = services();
        auto it = s.find(std::type_index(typeid(Interface)));
        if (it == s.end()) {
            throw std::runtime_error(std::string("Service not found in ServiceLocator: ") + typeid(Interface).name());
        }
        return std::any_cast<std::shared_ptr<Interface>>(it->second);
    }

    /// Checks if a service is currently registered.
    template <typename Interface>
    [[nodiscard]] static bool has() {
        std::lock_guard<std::mutex> lock(mutex());
        auto& s = services();
        return s.find(std::type_index(typeid(Interface))) != s.end();
    }

    /// Clears all registered services. Useful between tests.
    static void clear() {
        std::lock_guard<std::mutex> lock(mutex());
        services().clear();
    }

private:
    [[nodiscard]] static std::unordered_map<std::type_index, std::any>& services();
    [[nodiscard]] static std::mutex& mutex();
};

} // namespace nk