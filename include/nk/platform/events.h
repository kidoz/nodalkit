#pragma once

/// @file events.h
/// @brief Platform-agnostic input and window events.
///
/// Platform backends convert native events into these types and
/// deliver them through Window to the controller system.

#include <nk/actions/shortcut.h>
#include <nk/platform/key_codes.h>

namespace nk {

/// Mouse / pointer event.
struct MouseEvent {
    enum class Type : uint8_t {
        Press,
        Release,
        Move,
        Enter,
        Leave,
        Scroll,
    };

    Type type{};
    float x = 0;          ///< Position relative to the window.
    float y = 0;
    int button = 0;        ///< 1=left, 2=right, 3=middle (Press/Release only).
    float scroll_dx = 0;   ///< Horizontal scroll delta (Scroll only).
    float scroll_dy = 0;   ///< Vertical scroll delta (Scroll only).
    Modifiers modifiers = Modifiers::None;
};

/// Keyboard event.
struct KeyEvent {
    enum class Type : uint8_t {
        Press,
        Release,
    };

    Type type{};
    KeyCode key = KeyCode::Unknown;
    Modifiers modifiers = Modifiers::None;
    bool is_repeat = false; ///< True for auto-repeat key events.
};

/// Window-level event.
struct WindowEvent {
    enum class Type : uint8_t {
        Resize,
        Close,
        FocusIn,
        FocusOut,
        Expose, ///< Window needs redraw (e.g. after uncover).
    };

    Type type{};
    int width = 0;   ///< New size (Resize only).
    int height = 0;
};

} // namespace nk
