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

/// A text render node.
class TextNode : public RenderNode {
public:
    TextNode(Point origin, std::string text, Color color, FontDescriptor font = {});

    [[nodiscard]] const std::string& text() const;
    [[nodiscard]] Color text_color() const;
    [[nodiscard]] const FontDescriptor& font() const;

private:
    std::string text_;
    Color color_;
    FontDescriptor font_;
};

} // namespace nk
