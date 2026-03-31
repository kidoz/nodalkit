#pragma once

/// @file cursor_shape.h
/// @brief Cursor-shape hints produced by widgets and consumed by windows.

namespace nk {

/// Cursor shape requested by the currently hovered widget.
enum class CursorShape {
    Default,
    IBeam,
    PointingHand,
    ResizeLeftRight,
    ResizeUpDown,
};

} // namespace nk
