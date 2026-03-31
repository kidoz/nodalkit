#pragma once

/// @file types.h
/// @brief Core geometric and utility types for NodalKit.

#include <cmath>
#include <cstdint>
#include <limits>

namespace nk {

/// 2D point in logical coordinates.
struct Point {
    float x = 0;
    float y = 0;

    constexpr bool operator==(Point const&) const = default;
};

/// 2D size.
struct Size {
    float width = 0;
    float height = 0;

    constexpr bool operator==(Size const&) const = default;
};

/// Axis-aligned rectangle in logical coordinates.
struct Rect {
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;

    [[nodiscard]] constexpr float right() const { return x + width; }
    [[nodiscard]] constexpr float bottom() const { return y + height; }
    [[nodiscard]] constexpr Point origin() const { return {x, y}; }
    [[nodiscard]] constexpr Size size() const { return {width, height}; }
    [[nodiscard]] constexpr bool contains(Point p) const {
        return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
    }

    constexpr bool operator==(Rect const&) const = default;
};

/// Insets (padding/margin).
struct Insets {
    float top = 0;
    float right = 0;
    float bottom = 0;
    float left = 0;

    static constexpr Insets uniform(float v) { return {v, v, v, v}; }
    static constexpr Insets symmetric(float vertical, float horizontal) {
        return {vertical, horizontal, vertical, horizontal};
    }

    constexpr bool operator==(Insets const&) const = default;
};

/// RGBA color, components in [0, 1].
struct Color {
    float r = 0;
    float g = 0;
    float b = 0;
    float a = 1;

    static constexpr Color from_rgb(uint8_t r, uint8_t g, uint8_t b) {
        return {r / 255.0f, g / 255.0f, b / 255.0f, 1.0f};
    }

    constexpr bool operator==(Color const&) const = default;
};

/// Layout orientation.
enum class Orientation { Horizontal, Vertical };

/// Horizontal alignment.
enum class HAlign { Start, Center, End, Fill };

/// Vertical alignment.
enum class VAlign { Start, Center, End, Fill };

} // namespace nk
