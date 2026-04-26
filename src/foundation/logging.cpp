#include <cstdio>
#include <nk/foundation/logging.h>

namespace nk {

namespace {
LogLevel g_min_level = LogLevel::Info; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

constexpr const char* level_str(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Info:
        return "INFO ";
    case LogLevel::Warning:
        return "WARN ";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Fatal:
        return "FATAL";
    }
    return "?????";
}
} // namespace

void set_log_level(LogLevel level) {
    g_min_level = level;
}

LogLevel log_level() {
    return g_min_level;
}

void log(LogLevel level, std::string_view tag, std::string_view message) {
    if (level < g_min_level) {
        return;
    }
    std::fprintf(stderr,
                 "[%s] %.*s: %.*s\n",
                 level_str(level),
                 static_cast<int>(tag.size()),
                 tag.data(),
                 static_cast<int>(message.size()),
                 message.data());
}

namespace detail {
bool should_log(LogLevel level) {
    return level >= g_min_level;
}
} // namespace detail

} // namespace nk
