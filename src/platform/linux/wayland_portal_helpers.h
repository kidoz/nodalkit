#pragma once

/// @file wayland_portal_helpers.h
/// @brief Internal helpers for XDG Desktop Portal file-dialog behavior.

#include <cstdint>
#include <nk/platform/file_dialog.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nk::detail {

enum class PortalFilterKind : uint32_t {
    GlobPattern = 0,
    MimeType = 1,
};

struct PortalFilterEntry {
    PortalFilterKind kind = PortalFilterKind::GlobPattern;
    std::string value;

    constexpr bool operator==(const PortalFilterEntry&) const = default;
};

[[nodiscard]] std::string sanitize_dbus_unique_name(std::string_view unique_name);

[[nodiscard]] std::string portal_request_path(std::string_view unique_name,
                                              std::string_view handle_token);

[[nodiscard]] std::optional<PortalFilterEntry> portal_filter_value(std::string_view filter);

[[nodiscard]] std::vector<PortalFilterEntry>
portal_filter_entries(const std::vector<std::string>& filters);

[[nodiscard]] OpenFileDialogResult
portal_result_from_response(uint32_t response_code, std::optional<std::string> selected_path);

} // namespace nk::detail
