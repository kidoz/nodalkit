#pragma once

/// @file image_view.h
/// @brief Image display widget for raw pixel buffers.

#include <cstdint>
#include <memory>
#include <mutex>
#include <nk/render/image_node.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// Displays a raw ARGB8888 pixel buffer, scaled to fit the widget area.
/// Thread-safe: update_pixel_buffer() can be called from any thread.
class ImageView : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ImageView> create();
    ~ImageView() override;

    /// Update the displayed image. Copies the data internally.
    /// Thread-safe — can be called from the emulation thread.
    void update_pixel_buffer(const uint32_t* data, int width, int height);

    /// Set scaling mode (nearest-neighbor or bilinear).
    void set_scale_mode(ScaleMode mode);
    [[nodiscard]] ScaleMode scale_mode() const;

    /// Whether to preserve the source aspect ratio.
    void set_preserve_aspect_ratio(bool preserve);
    [[nodiscard]] bool preserve_aspect_ratio() const;

    /// Source image dimensions (0x0 if no image set).
    [[nodiscard]] int source_width() const;
    [[nodiscard]] int source_height() const;

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    ImageView();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
