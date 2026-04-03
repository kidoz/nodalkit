#pragma once

/// @file native_menu.h
/// @brief Shared application and widget menu model.

#include <cstdint>
#include <nk/platform/key_codes.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace nk {

/// Modifier flags used by native app-menu shortcuts.
enum class NativeMenuModifier : uint32_t {
    None = 0,
    Shift = 1u << 0,
    Ctrl = 1u << 1,
    Alt = 1u << 2,
    Super = 1u << 3,
};

constexpr NativeMenuModifier operator|(NativeMenuModifier a, NativeMenuModifier b) {
    return static_cast<NativeMenuModifier>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr NativeMenuModifier operator&(NativeMenuModifier a, NativeMenuModifier b) {
    return static_cast<NativeMenuModifier>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/// Keyboard shortcut descriptor for native app-menu items.
struct NativeMenuShortcut {
    KeyCode key = KeyCode::Unknown;
    NativeMenuModifier modifiers = NativeMenuModifier::None;
};

/// A single menu item. May either activate an action or own submenu children.
struct NativeMenuItem {
    std::string label;
    std::string action_name;
    bool enabled = true;
    bool separator = false;
    std::optional<NativeMenuShortcut> shortcut;
    std::vector<NativeMenuItem> children;

    /// Create an action item with a label, action name, and optional shortcut.
    static NativeMenuItem action(std::string label,
                                 std::string action,
                                 std::optional<NativeMenuShortcut> shortcut = std::nullopt) {
        NativeMenuItem item;
        item.label = std::move(label);
        item.action_name = std::move(action);
        item.shortcut = shortcut;
        return item;
    }

    /// Create a submenu item containing child items.
    static NativeMenuItem submenu(std::string label, std::vector<NativeMenuItem> items) {
        NativeMenuItem item;
        item.label = std::move(label);
        item.children = std::move(items);
        return item;
    }

    /// Create a separator line.
    static NativeMenuItem make_separator() {
        NativeMenuItem item;
        item.separator = true;
        return item;
    }
};

/// A top-level menu in a menu bar or native app-menu model.
struct NativeMenu {
    std::string title;
    std::vector<NativeMenuItem> items;
};

} // namespace nk
