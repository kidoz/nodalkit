#pragma once

/// @file shortcut.h
/// @brief Keyboard shortcut descriptors and bindings.

#include <cstdint>
#include <string>

namespace nk {

/// Modifier key flags.
enum class Modifiers : uint32_t {
    None = 0,
    Shift = 1u << 0,
    Ctrl = 1u << 1,
    Alt = 1u << 2,
    Super = 1u << 3,
};

constexpr Modifiers operator|(Modifiers a, Modifiers b) {
    return static_cast<Modifiers>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr Modifiers operator&(Modifiers a, Modifiers b) {
    return static_cast<Modifiers>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

/// A keyboard shortcut descriptor (e.g. Ctrl+S).
struct Shortcut {
    int key_code = 0;
    Modifiers modifiers = Modifiers::None;
};

/// Binds a shortcut to an action name.
struct ShortcutBinding {
    Shortcut shortcut;
    std::string action_name;
};

} // namespace nk
