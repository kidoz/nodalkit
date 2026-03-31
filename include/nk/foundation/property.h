#pragma once

/// @file property.h
/// @brief Observable property with change notification and one-way binding.

#include <nk/foundation/signal.h>

#include <utility>

namespace nk {

/// An observable value that emits a signal when changed.
///
/// Usage:
/// @code
///   nk::Property<int> counter{0};
///   counter.on_changed().connect([](int const& v) {
///       std::println("counter = {}", v);
///   });
///   counter.set(42);  // prints: counter = 42
/// @endcode
template <typename T>
class Property {
public:
    explicit Property(T initial = T{}) : value_(std::move(initial)) {}

    /// Get the current value.
    [[nodiscard]] T const& get() const { return value_; }

    /// Implicit conversion to the underlying type.
    [[nodiscard]] operator T const&() const { return value_; } // NOLINT(google-explicit-constructor)

    /// Set a new value. Emits on_changed() if the value actually differs.
    void set(T value) {
        if (!(value_ == value)) {
            value_ = std::move(value);
            changed_.emit(value_);
        }
    }

    /// Signal emitted after the value changes.
    [[nodiscard]] Signal<T>& on_changed() { return changed_; }

    /// One-way binding: this property tracks `source`.
    /// Returns a ScopedConnection — the binding stays alive as long as it does.
    [[nodiscard]] ScopedConnection bind_to(Property<T>& source) {
        set(source.get());
        return ScopedConnection(
            source.on_changed().connect([this](T const& v) { set(v); }));
    }

private:
    T value_;
    Signal<T> changed_;
};

} // namespace nk
