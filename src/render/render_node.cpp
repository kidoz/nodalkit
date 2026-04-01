#include <nk/render/render_node.h>

namespace nk {

RenderNode::RenderNode(RenderNodeKind kind) : kind_(kind) {}

RenderNode::~RenderNode() = default;

RenderNodeKind RenderNode::kind() const {
    return kind_;
}

const Rect& RenderNode::bounds() const {
    return bounds_;
}

void RenderNode::set_bounds(Rect bounds) {
    bounds_ = bounds;
}

void RenderNode::set_debug_source(std::string label, std::span<const std::size_t> path) {
    debug_source_label_ = std::move(label);
    debug_source_path_.assign(path.begin(), path.end());
}

std::string_view RenderNode::debug_source_label() const {
    return debug_source_label_;
}

std::span<const std::size_t> RenderNode::debug_source_path() const {
    return debug_source_path_;
}

void RenderNode::append_child(std::unique_ptr<RenderNode> child) {
    children_.push_back(std::move(child));
}

const std::vector<std::unique_ptr<RenderNode>>& RenderNode::children() const {
    return children_;
}

// --- ColorRectNode ---

ColorRectNode::ColorRectNode(Rect rect, Color color)
    : RenderNode(RenderNodeKind::ColorRect), color_(color) {
    set_bounds(rect);
}

Color ColorRectNode::color() const {
    return color_;
}

// --- RoundedRectNode ---

RoundedRectNode::RoundedRectNode(Rect rect, Color color, float corner_radius)
    : RenderNode(RenderNodeKind::RoundedRect), color_(color), corner_radius_(corner_radius) {
    set_bounds(rect);
}

Color RoundedRectNode::color() const {
    return color_;
}

float RoundedRectNode::corner_radius() const {
    return corner_radius_;
}

// --- BorderNode ---

BorderNode::BorderNode(Rect rect, Color color, float thickness, float corner_radius)
    : RenderNode(RenderNodeKind::Border)
    , color_(color)
    , thickness_(thickness)
    , corner_radius_(corner_radius) {
    set_bounds(rect);
}

Color BorderNode::color() const {
    return color_;
}

float BorderNode::thickness() const {
    return thickness_;
}

float BorderNode::corner_radius() const {
    return corner_radius_;
}

// --- RoundedClipNode ---

RoundedClipNode::RoundedClipNode(Rect rect, float corner_radius)
    : RenderNode(RenderNodeKind::RoundedClip), corner_radius_(corner_radius) {
    set_bounds(rect);
}

float RoundedClipNode::corner_radius() const {
    return corner_radius_;
}

// --- TextNode ---

TextNode::TextNode(Point origin, std::string text, Color color, FontDescriptor font)
    : RenderNode(RenderNodeKind::Text)
    , text_(std::move(text))
    , color_(color)
    , font_(std::move(font)) {
    // Approximate bounds; real implementation needs text metrics.
    set_bounds({origin.x, origin.y, 0, 0});
}

const std::string& TextNode::text() const {
    return text_;
}

Color TextNode::text_color() const {
    return color_;
}

const FontDescriptor& TextNode::font() const {
    return font_;
}

} // namespace nk
