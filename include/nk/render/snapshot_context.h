#pragma once

/// @file snapshot_context.h
/// @brief Context for building render node trees during widget snapshots.

#include <memory>
#include <nk/foundation/types.h>
#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <nk/text/font.h>
#include <span>
#include <string>
#include <vector>

namespace nk {

/// Context passed to Widget::snapshot() for building render node trees.
/// Widgets append render nodes to the context; the Window collects
/// the root node after the snapshot pass.
class SnapshotContext {
public:
    SnapshotContext();
    ~SnapshotContext();

    /// Add a render node to the current container.
    void add_node(std::unique_ptr<RenderNode> node);

    /// Convenience: add a solid color rectangle.
    void add_color_rect(Rect rect, Color color);

    /// Convenience: add a solid rounded rectangle.
    void add_rounded_rect(Rect rect, Color color, float corner_radius);

    /// Convenience: add a border stroke around a rectangle.
    void add_border(Rect rect, Color color, float thickness = 1.0F, float corner_radius = 0.0F);

    /// Convenience: add a text node.
    void add_text(Point origin, std::string text, Color color, FontDescriptor font = {});

    /// Convenience: add an image node.
    void add_image(Rect dest,
                   const uint32_t* data,
                   int w,
                   int h,
                   ScaleMode scale = ScaleMode::NearestNeighbor);

    /// Push a child container (for grouping/clipping).
    void push_container(Rect bounds);

    /// Enable or disable debug source annotation on render nodes.
    /// When disabled, push_debug_source/pop_debug_source become no-ops
    /// and snapshot_subtree skips expensive tree-path computation.
    void set_debug_annotations_enabled(bool enabled);
    [[nodiscard]] bool debug_annotations_enabled() const;

    /// Associate subsequently added render nodes with a source widget.
    void push_debug_source(std::string label, std::span<const std::size_t> path);
    void pop_debug_source();

    /// Push a rounded clip node. Children rendered inside it are masked to the
    /// provided rounded rectangle.
    void push_rounded_clip(Rect bounds, float corner_radius);

    /// Push a container into the overlay layer, rendered after normal
    /// content. Useful for popups and transient surfaces.
    void push_overlay_container(Rect bounds);

    /// Pop the current container.
    void pop_container();

    /// Get the root render node (transfers ownership).
    [[nodiscard]] std::unique_ptr<RenderNode> take_root();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
