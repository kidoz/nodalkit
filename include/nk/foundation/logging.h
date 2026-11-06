#pragma once

/// @file logging.h
/// @brief Minimal logging facilities for NodalKit internals and applications.

#include <cstdio>
#include <source_location>
#include <string_view>

namespace nk {

/// Log severity levels.
enum class LogLevel { Trace, Debug, Info, Warning, Error, Fatal };

/// Set the minimum log level. Messages below this level are discarded.
void set_log_level(LogLevel level);

/// Get the current minimum log level.
[[nodiscard]] LogLevel log_level();

/// Log a message. The `NK_LOG_*` macros are the usual callers. `where` records
/// the call site; a default argument captures the caller, including a call
/// expanded from those macros.
void log(LogLevel level,
         std::string_view tag,
         std::string_view message,
         const std::source_location& where = std::source_location::current());

namespace detail {
[[nodiscard]] bool should_log(LogLevel level);
} // namespace detail

} // namespace nk

// Convenience logging. These are the only public-API macros in NodalKit.
// The level check stays outside log() so a disabled message is not formatted,
// and the call still records the expansion site through log()'s default
// source_location argument.
// NOLINTBEGIN(cppcoreguidelines-macro-usage)
#define NK_LOG_TRACE(tag, msg)                                                                     \
    do {                                                                                           \
        if (nk::detail::should_log(nk::LogLevel::Trace))                                           \
            nk::log(nk::LogLevel::Trace, (tag), (msg));                                            \
    } while (false)

#define NK_LOG_DEBUG(tag, msg)                                                                     \
    do {                                                                                           \
        if (nk::detail::should_log(nk::LogLevel::Debug))                                           \
            nk::log(nk::LogLevel::Debug, (tag), (msg));                                            \
    } while (false)

#define NK_LOG_INFO(tag, msg)                                                                      \
    do {                                                                                           \
        if (nk::detail::should_log(nk::LogLevel::Info))                                            \
            nk::log(nk::LogLevel::Info, (tag), (msg));                                             \
    } while (false)

#define NK_LOG_WARN(tag, msg)                                                                      \
    do {                                                                                           \
        if (nk::detail::should_log(nk::LogLevel::Warning))                                         \
            nk::log(nk::LogLevel::Warning, (tag), (msg));                                          \
    } while (false)

#define NK_LOG_ERROR(tag, msg)                                                                     \
    do {                                                                                           \
        if (nk::detail::should_log(nk::LogLevel::Error))                                           \
            nk::log(nk::LogLevel::Error, (tag), (msg));                                            \
    } while (false)
// NOLINTEND(cppcoreguidelines-macro-usage)
