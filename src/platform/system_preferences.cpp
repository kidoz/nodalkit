#include <nk/platform/system_preferences.h>

namespace nk {

std::string_view to_string(PlatformFamily family) {
    switch (family) {
    case PlatformFamily::Linux:
        return "linux";
    case PlatformFamily::Windows:
        return "windows";
    case PlatformFamily::MacOS:
        return "macos";
    case PlatformFamily::Unknown:
    default:
        return "unknown";
    }
}

std::string_view to_string(DesktopEnvironment desktop_environment) {
    switch (desktop_environment) {
    case DesktopEnvironment::Gnome:
        return "gnome";
    case DesktopEnvironment::KDE:
        return "kde";
    case DesktopEnvironment::Other:
        return "other";
    case DesktopEnvironment::Unknown:
    default:
        return "unknown";
    }
}

} // namespace nk
