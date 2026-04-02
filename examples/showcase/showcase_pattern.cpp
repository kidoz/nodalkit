/// @file showcase_pattern.cpp
/// @brief Test-pattern generation for the showcase preview canvas.

#include "showcase_pattern.h"

#include <cstddef>
#include <cstdint>

std::vector<std::uint32_t> generate_test_pattern(int width, int height, int frame) {
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto r = static_cast<std::uint8_t>((x + frame) % 256);
            const auto g = static_cast<std::uint8_t>((y + frame / 2) % 256);
            const auto b = static_cast<std::uint8_t>(((x + y) + frame) % 256);
            pixels[static_cast<std::size_t>(y * width + x)] =
                0xFF000000U | (static_cast<std::uint32_t>(r) << 16) |
                (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(b);
        }
    }
    return pixels;
}
