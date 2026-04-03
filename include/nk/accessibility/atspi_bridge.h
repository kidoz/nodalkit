#pragma once

/// @file atspi_bridge.h
/// @brief AT-SPI2 snapshot helpers for Linux accessibility backends.

#include <cstdint>
#include <nk/debug/diagnostics.h>
#include <nk/foundation/types.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

enum class AtspiStateBit : uint32_t {
    None = 0,
    Enabled = 1U << 0,
    Sensitive = 1U << 1,
    Visible = 1U << 2,
    Showing = 1U << 3,
    Focusable = 1U << 4,
    Focused = 1U << 5,
    Hovered = 1U << 6,
    Pressed = 1U << 7,
};

[[nodiscard]] constexpr AtspiStateBit operator|(AtspiStateBit lhs, AtspiStateBit rhs) noexcept {
    return static_cast<AtspiStateBit>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr AtspiStateBit& operator|=(AtspiStateBit& lhs, AtspiStateBit rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

[[nodiscard]] constexpr bool has_atspi_state(AtspiStateBit value, AtspiStateBit bit) noexcept {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(bit)) != 0U;
}

struct AtspiAccessibleNode {
    std::string object_name;
    std::string object_path;
    std::string parent_path;
    std::string role_name;
    std::string name;
    std::string description;
    std::string value;
    Rect bounds{};
    AtspiStateBit state = AtspiStateBit::None;
    std::vector<std::size_t> tree_path;
    std::vector<std::string> action_names;
    std::vector<std::string> relations;
    std::vector<std::string> child_paths;
    std::vector<std::string> interfaces;

    bool operator==(const AtspiAccessibleNode&) const = default;
};

struct AtspiAccessibleSnapshot {
    std::vector<AtspiAccessibleNode> nodes;

    bool operator==(const AtspiAccessibleSnapshot&) const = default;
};

[[nodiscard]] std::string atspi_sanitize_object_name(std::string_view value);
[[nodiscard]] std::string atspi_role_name(std::string_view accessible_role);
[[nodiscard]] AtspiStateBit atspi_state_bits(const WidgetDebugNode& node);
[[nodiscard]] AtspiAccessibleSnapshot
build_atspi_window_snapshot(std::string_view subtree_root_path,
                            std::string_view window_object_name,
                            std::string_view window_title,
                            Rect window_bounds,
                            const WidgetDebugNode& widget_root);
[[nodiscard]] const AtspiAccessibleNode*
find_atspi_accessible_node(const AtspiAccessibleSnapshot& snapshot,
                           std::string_view object_path) noexcept;

} // namespace nk
