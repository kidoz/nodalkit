#pragma once

/// @file native_toolbar.h
/// @brief Declarative description of a window-attached native toolbar.

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// A single item in a native toolbar.
///
/// Items are identified by a stable string `identifier`. The identifier is
/// used by the platform to persist user customizations (reordering, hiding)
/// across runs, so it must remain stable across application versions.
struct NativeToolbarItem {
    enum class Kind : std::uint8_t {
        /// A clickable button. Uses `symbol_name` as its glyph on macOS when
        /// set, falling back to `label` text otherwise.
        Button,
        /// A single-line search field that forwards its current text to
        /// `on_search_submit` when the user commits.
        SearchField,
        /// Expands to fill remaining space, pushing items on either side to
        /// opposite edges.
        FlexibleSpace,
        /// Fixed-width space.
        Space,
        /// Visual separator (thin vertical divider).
        Separator,
    };

    Kind kind = Kind::Button;
    /// Stable identifier used for persistence of user customization. Must be
    /// unique within a toolbar.
    std::string identifier;
    /// Human-readable label shown under the item or in the customization
    /// palette.
    std::string label;
    /// Tooltip shown on hover.
    std::string tooltip;
    /// macOS SF Symbol name for the icon glyph. Ignored on platforms without
    /// SF Symbols. When empty, falls back to a text-only button.
    std::string symbol_name;
    /// Fired when a `Button` item is activated.
    std::function<void()> on_activate;
    /// Fired when a `SearchField` item commits its text (e.g., Enter).
    std::function<void(std::string_view)> on_search_submit;
};

/// Configuration for a native window toolbar.
struct NativeToolbarConfig {
    /// Unique identifier used by the platform to autosave user customizations.
    /// Apps should pick a stable string like "com.example.MainToolbar".
    std::string identifier;
    /// Full catalog of items. All items available to the user (including ones
    /// hidden by default) must appear here.
    std::vector<NativeToolbarItem> items;
    /// Item identifiers shown by default, in order. When empty, every item in
    /// `items` is shown in declaration order.
    std::vector<std::string> default_item_identifiers;
    /// Whether the user may reorder or hide items. Defaults to true.
    bool allows_user_customization = true;
};

} // namespace nk
