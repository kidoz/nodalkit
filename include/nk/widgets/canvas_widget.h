#pragma once

/// @file canvas_widget.h
/// @brief Specialized widget for custom drawing and off-screen buffering.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

class SnapshotContext;

/// A specialized widget for high-performance off-screen drawing or custom graph rendering.
/// In a fully accelerated pipeline, this widget signals the renderer to cache its
/// contents in an off-screen texture buffer until `queue_canvas_redraw()` is called.
class CanvasWidget : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<CanvasWidget> create();

    ~CanvasWidget() override;

    /// Signal emitted when the canvas needs to render its content.
    /// Observers should use the provided SnapshotContext to emit drawing commands.
    Signal<SnapshotContext&, Rect>& on_draw();

    /// Mark the canvas as needing a redraw, flushing any cached off-screen buffers.
    void queue_canvas_redraw();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    CanvasWidget();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk