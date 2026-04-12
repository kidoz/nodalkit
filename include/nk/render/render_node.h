#pragma once

/// @file render_node.h
/// @brief Retained render node tree for the painting pipeline.

#include <memory>
#include <nk/foundation/types.h>
#include <nk/text/font.h>
#include <span>
#include <string>
#include <vector>

namespace nk {

/// Kind of render node.
enum class RenderNodeKind {
    Container,   ///< Groups children; applies a transform/clip.
    ColorRect,   ///< Solid color rectangle.
    RoundedRect, ///< Solid rounded rectangle.
    Text,        ///< Shaped text run.
    Image,       ///< Raster image.
    Border,      ///< Border stroke around a rectangle.
    RoundedClip, ///< Rounded-rectangle clip region.
    Opacity,        ///< Multiplies children's alpha by a factor.
    LinearGradient, ///< Linear gradient fill.
    Shadow,         ///< Box shadow (outset).
};

/// A node in the retained render tree. Widgets produce render nodes
/// during the snapshot phase; the renderer consumes them.
class RenderNode {
public:
    explicit RenderNode(RenderNodeKind kind);
    virtual ~RenderNode();

    RenderNode(const RenderNode&) = delete;
    RenderNode& operator=(const RenderNode&) = delete;

    [[nodiscard]] RenderNodeKind kind() const;
    [[nodiscard]] const Rect& bounds() const;
    void set_bounds(Rect bounds);
    void set_debug_source(std::string label, std::span<const std::size_t> path);
    [[nodiscard]] std::string_view debug_source_label() const;
    [[nodiscard]] std::span<const std::size_t> debug_source_path() const;

    void append_child(std::unique_ptr<RenderNode> child);
    [[nodiscard]] const std::vector<std::unique_ptr<RenderNode>>& children() const;

private:
    RenderNodeKind kind_;
    Rect bounds_{};
    std::string debug_source_label_;
    std::vector<std::size_t> debug_source_path_;
    std::vector<std::unique_ptr<RenderNode>> children_;
};

/// A solid color rectangle render node.
class ColorRectNode : public RenderNode {
public:
    ColorRectNode(Rect rect, Color color);

    [[nodiscard]] Color color() const;

private:
    Color color_;
};

/// A solid rounded rectangle render node.
class RoundedRectNode : public RenderNode {
public:
    RoundedRectNode(Rect rect, Color color, float corner_radius);

    [[nodiscard]] Color color() const;
    [[nodiscard]] float corner_radius() const;

private:
    Color color_;
    float corner_radius_ = 0.0F;
};

/// A border stroke render node. Can be square or rounded.
class BorderNode : public RenderNode {
public:
    BorderNode(Rect rect, Color color, float thickness, float corner_radius = 0.0F);

    [[nodiscard]] Color color() const;
    [[nodiscard]] float thickness() const;
    [[nodiscard]] float corner_radius() const;

private:
    Color color_;
    float thickness_ = 1.0F;
    float corner_radius_ = 0.0F;
};

/// A clip node that restricts its children to a rounded rectangle.
class RoundedClipNode : public RenderNode {
public:
    RoundedClipNode(Rect rect, float corner_radius);

    [[nodiscard]] float corner_radius() const;

private:
    float corner_radius_ = 0.0F;
};

/// A text render node. The `size` argument supplies the text's pre-measured
/// logical pixel dimensions so the node's `bounds()` rect spans the rendered
/// text; `{origin.x, origin.y, size.width, size.height}`. Callers that do not
/// have a measured size may pass `{}` and will receive zero-width bounds,
/// but hit testing and clipping will not work correctly for those nodes.
class TextNode : public RenderNode {
public:
    TextNode(Point origin, Size size, std::string text, Color color, FontDescriptor font = {});

    [[nodiscard]] const std::string& text() const;
    [[nodiscard]] Color text_color() const;
    [[nodiscard]] const FontDescriptor& font() const;

private:
    std::string text_;
    Color color_;
    FontDescriptor font_;
};

/// An opacity container node. Multiplies the effective alpha of all
/// descendant draw calls by its opacity factor [0, 1]. Used for disabled
/// states, fade effects, and any case where a subtree needs uniform
/// transparency. This is a simple per-draw-call alpha multiplier, not a
/// composited group — overlapping siblings within the node may show
/// through each other at low opacity values.
class OpacityNode : public RenderNode {
public:
    OpacityNode(Rect bounds, float opacity);

    [[nodiscard]] float opacity() const;

private:
    float opacity_ = 1.0F;
};

/// A linear gradient fill render node. Interpolates between `start_color`
/// and `end_color` along the specified orientation across the bounds rect.
class LinearGradientNode : public RenderNode {
public:
    LinearGradientNode(Rect bounds, Color start_color, Color end_color,
                       Orientation direction = Orientation::Vertical);

    [[nodiscard]] Color start_color() const;
    [[nodiscard]] Color end_color() const;
    [[nodiscard]] Orientation direction() const;

private:
    Color start_color_;
    Color end_color_;
    Orientation direction_ = Orientation::Vertical;
};

/// An outset box shadow render node. Draws a shadow behind a rectangle
/// with the given offset, blur radius, spread, corner radius, and color.
/// The blur is approximated by a distance-based alpha falloff from the
/// shadow rect boundary — not a true Gaussian convolution — which is
/// sufficient for elevation cues and card surfaces at typical blur radii.
class ShadowNode : public RenderNode {
public:
    ShadowNode(Rect rect, Color color, float offset_x, float offset_y, float blur_radius,
               float spread = 0.0F, float corner_radius = 0.0F);

    [[nodiscard]] Color color() const;
    [[nodiscard]] float offset_x() const;
    [[nodiscard]] float offset_y() const;
    [[nodiscard]] float blur_radius() const;
    [[nodiscard]] float spread() const;
    [[nodiscard]] float corner_radius() const;

private:
    Color color_;
    float offset_x_ = 0.0F;
    float offset_y_ = 0.0F;
    float blur_radius_ = 0.0F;
    float spread_ = 0.0F;
    float corner_radius_ = 0.0F;
};

} // namespace nk
