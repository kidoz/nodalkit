#include "wayland_portal_helpers.h"

namespace nk::detail {

std::string sanitize_dbus_unique_name(std::string_view unique_name) {
    if (unique_name.empty()) {
        return {};
    }

    std::string sanitized(unique_name);
    if (!sanitized.empty() && sanitized.front() == ':') {
        sanitized.erase(sanitized.begin());
    }

    for (auto& character : sanitized) {
        if (character == '.') {
            character = '_';
        }
    }

    return sanitized;
}

std::string portal_request_path(std::string_view unique_name, std::string_view handle_token) {
    return "/org/freedesktop/portal/desktop/request/" + sanitize_dbus_unique_name(unique_name) +
           "/" + std::string(handle_token);
}

std::optional<PortalFilterEntry> portal_filter_value(std::string_view filter) {
    if (filter.empty()) {
        return std::nullopt;
    }

    if (filter.find('/') != std::string_view::npos) {
        return PortalFilterEntry{.kind = PortalFilterKind::MimeType, .value = std::string(filter)};
    }

    if (filter.starts_with("*.")) {
        return PortalFilterEntry{.kind = PortalFilterKind::GlobPattern,
                                 .value = std::string(filter)};
    }
    if (filter.starts_with('.')) {
        return PortalFilterEntry{.kind = PortalFilterKind::GlobPattern,
                                 .value = "*" + std::string(filter)};
    }
    if (filter.find('*') != std::string_view::npos || filter.find('?') != std::string_view::npos) {
        return PortalFilterEntry{.kind = PortalFilterKind::GlobPattern,
                                 .value = std::string(filter)};
    }

    return PortalFilterEntry{.kind = PortalFilterKind::GlobPattern,
                             .value = "*." + std::string(filter)};
}

std::vector<PortalFilterEntry> portal_filter_entries(const std::vector<std::string>& filters) {
    std::vector<PortalFilterEntry> entries;
    entries.reserve(filters.size());

    for (const auto& filter : filters) {
        if (auto entry = portal_filter_value(filter)) {
            entries.push_back(std::move(*entry));
        }
    }

    return entries;
}

OpenFileDialogResult portal_result_from_response(uint32_t response_code,
                                                 std::optional<std::string> selected_path) {
    if (response_code == 0) {
        if (selected_path.has_value()) {
            return std::move(*selected_path);
        }
        return Unexpected(FileDialogError::Failed);
    }

    if (response_code == 1) {
        return Unexpected(FileDialogError::Cancelled);
    }

    return Unexpected(FileDialogError::Failed);
}

} // namespace nk::detail
