#pragma once

/// @file style_class.h
/// @brief CSS-like style selector and class primitives.

#include <nk/foundation/types.h>
#include <nk/ui_core/state_flags.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace nk {

/// A single style property value.
using StyleValue = std::variant<float, Color, std::string>;

/// A CSS-like selector that matches widgets by type name, style class,
/// and/or pseudo-state.
struct StyleSelector {
    std::string type_name;                      ///< e.g. "Button", "" means any.
    std::vector<std::string> classes;           ///< e.g. {"destructive", "flat"}.
    StateFlags pseudo_state = StateFlags::None; ///< e.g. Hovered|Pressed.

    /// Specificity for cascade ordering: (type, classes, pseudo).
    [[nodiscard]] int specificity() const;
};

/// A style rule: selector + property map.
struct StyleRule {
    StyleSelector selector;
    std::unordered_map<std::string, StyleValue> properties;
};

} // namespace nk
