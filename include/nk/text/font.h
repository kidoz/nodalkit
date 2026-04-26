#pragma once

/// @file font.h
/// @brief Font descriptor types.

#include <cstdint>
#include <string>

namespace nk {

/// Font weight.
enum class FontWeight : uint16_t {
    Thin = 100,
    Light = 300,
    Regular = 400,
    Medium = 500,
    SemiBold = 600,
    Bold = 700,
    ExtraBold = 800,
    Black = 900,
};

/// Font style.
enum class FontStyle : uint8_t {
    Normal,
    Italic,
    Oblique,
};

/// Describes a font for text rendering.
struct FontDescriptor {
    std::string family; ///< Font family name (e.g. "System", "Menlo").
    float size = 13.0F; ///< Font size in points.
    FontWeight weight = FontWeight::Regular;
    FontStyle style = FontStyle::Normal;
};

} // namespace nk
