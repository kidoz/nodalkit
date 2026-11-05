#pragma once

#include <nk/platform/drag_drop.h>
#include <optional>
#include <string>
#include <string_view>

namespace nk::detail {

// Decode only local absolute file URIs; remote URLs are not local file drops.
[[nodiscard]] std::optional<DragPayload> decode_wayland_drop(std::string_view mime,
                                                             std::string data);

} // namespace nk::detail
