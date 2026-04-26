#pragma once

/// @file result.h
/// @brief Result<T, E> for fallible operations without exceptions.

#include <cassert>
#include <string>
#include <utility>
#include <variant>

namespace nk {

/// Wrapper that marks a value as an error for Result construction.
template <typename E> class Unexpected {
public:
    explicit Unexpected(E error) : error_(std::move(error)) {}

    [[nodiscard]] const E& error() const& { return error_; }

    [[nodiscard]] E& error() & { return error_; }

    [[nodiscard]] E&& error() && { return std::move(error_); }

private:
    E error_;
};

/// Deduction guide.
template <typename E> Unexpected(E) -> Unexpected<E>;

/// A value-or-error type. Models the same concept as std::expected (C++23)
/// but available under C++20.
template <typename T, typename E = std::string> class Result {
public:
    /// Implicit construction from a success value.
    Result(T value) : data_(std::move(value)) {} // NOLINT(google-explicit-constructor)

    /// Implicit construction from an error.
    Result(Unexpected<E> err) // NOLINT(google-explicit-constructor)
        : data_(std::move(err)) {}

    [[nodiscard]] bool has_value() const { return std::holds_alternative<T>(data_); }

    [[nodiscard]] explicit operator bool() const { return has_value(); }

    [[nodiscard]] T& value() & {
        assert(has_value());
        return std::get<T>(data_);
    }

    [[nodiscard]] const T& value() const& {
        assert(has_value());
        return std::get<T>(data_);
    }

    [[nodiscard]] T&& value() && {
        assert(has_value());
        return std::get<T>(std::move(data_));
    }

    [[nodiscard]] E& error() & {
        assert(!has_value());
        return std::get<Unexpected<E>>(data_).error();
    }

    [[nodiscard]] const E& error() const& {
        assert(!has_value());
        return std::get<Unexpected<E>>(data_).error();
    }

    T& operator*() & { return value(); }

    const T& operator*() const& { return value(); }

    T* operator->() { return &value(); }

    const T* operator->() const { return &value(); }

private:
    std::variant<T, Unexpected<E>> data_;
};

/// Specialization for void success type.
template <typename E> class Result<void, E> {
public:
    Result() = default;

    Result(Unexpected<E> err) // NOLINT(google-explicit-constructor)
        : error_(std::move(err.error())), has_error_(true) {}

    [[nodiscard]] bool has_value() const { return !has_error_; }

    [[nodiscard]] explicit operator bool() const { return has_value(); }

    [[nodiscard]] E& error() & {
        assert(has_error_);
        return error_;
    }

    [[nodiscard]] const E& error() const& {
        assert(has_error_);
        return error_;
    }

private:
    E error_{};
    bool has_error_ = false;
};

} // namespace nk
