/// @file platform_backend.cpp
/// @brief Factory for the platform-appropriate backend.

#include <nk/platform/platform_backend.h>

#ifdef __APPLE__
#include "macos/macos_backend.h"
#elif defined(_WIN32)
#include "windows/win32_backend.h"
#elif defined(__linux__)
#include "linux/wayland_backend.h"
#endif

namespace nk {

std::unique_ptr<PlatformBackend> PlatformBackend::create() {
#ifdef __APPLE__
    return std::make_unique<MacosBackend>();
#elif defined(_WIN32)
    return std::make_unique<Win32Backend>();
#elif defined(__linux__)
    return std::make_unique<WaylandBackend>();
#else
    return nullptr;
#endif
}

} // namespace nk
