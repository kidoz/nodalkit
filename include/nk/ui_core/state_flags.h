#pragma once

/// @file state_flags.h
/// @brief Widget state flags used for styling and accessibility.

#include <cstdint>

namespace nk {

/// Flags representing the interactive state of a widget.
/// Multiple flags can be combined. Used by the style engine and
/// accessibility layer.
enum class StateFlags : uint32_t {
    None        = 0,
    Hovered     = 1u << 0,
    Pressed     = 1u << 1,
    Focused     = 1u << 2,
    Disabled    = 1u << 3,
    Checked     = 1u << 4,
    Selected    = 1u << 5,
    Dragging    = 1u << 6,
};

constexpr StateFlags operator|(StateFlags a, StateFlags b) {
    return static_cast<StateFlags>(
        static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr StateFlags operator&(StateFlags a, StateFlags b) {
    return static_cast<StateFlags>(
        static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr StateFlags operator~(StateFlags a) {
    return static_cast<StateFlags>(~static_cast<uint32_t>(a));
}

constexpr bool has_flag(StateFlags flags, StateFlags test) {
    return (flags & test) == test;
}

} // namespace nk
