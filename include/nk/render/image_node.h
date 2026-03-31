#pragma once

/// @file image_node.h
/// @brief Render node for blitting raw pixel buffers.

#include <nk/render/render_node.h>

#include <cstdint>

namespace nk {

/// Scaling mode for image rendering.
enum class ScaleMode { NearestNeighbor, Bilinear };

/// Render node that blits a raw pixel buffer.
class ImageNode : public RenderNode {
public:
    /// @param dest       Destination rectangle in the render surface.
    /// @param pixel_data Pointer to ARGB8888 pixel data (not owned).
    /// @param src_width  Source image width in pixels.
    /// @param src_height Source image height in pixels.
    /// @param scale      Scaling mode.
    ImageNode(Rect dest, uint32_t const* pixel_data, int src_width,
              int src_height, ScaleMode scale = ScaleMode::NearestNeighbor);

    [[nodiscard]] uint32_t const* pixel_data() const;
    [[nodiscard]] int src_width() const;
    [[nodiscard]] int src_height() const;
    [[nodiscard]] ScaleMode scale_mode() const;

private:
    uint32_t const* pixel_data_;
    int src_width_;
    int src_height_;
    ScaleMode scale_mode_;
};

} // namespace nk
