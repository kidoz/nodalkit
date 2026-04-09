#pragma once

/// @file events.h
/// @brief Platform-agnostic input and window events.
///
/// Platform backends convert native events into these types and
/// deliver them through Window to the controller system.

#include <nk/actions/shortcut.h>
#include <nk/platform/key_codes.h>
#include <string>

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
    float x = 0; ///< Position relative to the window.
    float y = 0;
    int button = 0;                 ///< 1=left, 2=right, 3=middle (Press/Release only).
    float scroll_dx = 0;            ///< Horizontal scroll delta (Scroll only).
    float scroll_dy = 0;            ///< Vertical scroll delta (Scroll only).
    bool precise_scrolling = false; ///< Pixel-like scroll deltas (e.g. trackpad) when true.
    Modifiers modifiers = Modifiers::None;
    int click_count = 1; ///< 1 for single-click, 2 for double-click, etc.
};

/// Platform text-input event.
///
/// This is distinct from key events: key events describe physical key presses,
/// while text-input events describe committed or in-progress text produced by
/// the platform text system (including IME/preedit composition).
struct TextInputEvent {
    enum class Type : uint8_t {
        Commit,
        Preedit,
        ClearPreedit,
        DeleteSurrounding,
    };

    Type type{};
    std::string text;
    std::size_t selection_start = 0;      ///< Relative selection start within `text` for preedit.
    std::size_t selection_end = 0;        ///< Relative selection end within `text` for preedit.
    std::size_t delete_before_length = 0; ///< Bytes before the cursor to remove.
    std::size_t delete_after_length = 0;  ///< Bytes after the cursor to remove.
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
        ScaleFactorChanged,
        Close,
        FocusIn,
        FocusOut,
        Expose, ///< Window needs redraw (e.g. after uncover).
    };

    Type type{};
    int width = 0; ///< New size (Resize only).
    int height = 0;
    float scale_factor = 1.0F; ///< New content scale (ScaleFactorChanged only).
};

} // namespace nk
