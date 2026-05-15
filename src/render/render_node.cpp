#include <algorithm>
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

// --- LineNode ---

LineNode::LineNode(Point start, Point end, Color color, float thickness)
    : RenderNode(RenderNodeKind::Line)
    , start_(start)
    , end_(end)
    , color_(color)
    , thickness_(thickness) {
    const float min_x = std::min(start.x, end.x) - thickness / 2.0F;
    const float max_x = std::max(start.x, end.x) + thickness / 2.0F;
    const float min_y = std::min(start.y, end.y) - thickness / 2.0F;
    const float max_y = std::max(start.y, end.y) + thickness / 2.0F;
    set_bounds({min_x, min_y, max_x - min_x, max_y - min_y});
}

Point LineNode::start() const {
    return start_;
}

Point LineNode::end() const {
    return end_;
}

Color LineNode::color() const {
    return color_;
}

float LineNode::thickness() const {
    return thickness_;
}

// --- PathNode ---

PathNode::PathNode(Path2D path, Color stroke_color, float thickness)
    : RenderNode(RenderNodeKind::Path)
    , path_(std::move(path))
    , stroke_color_(stroke_color)
    , thickness_(thickness) {
    if (path_.points.empty()) {
        set_bounds({});
    } else {
        float min_x = path_.points[0].x;
        float max_x = path_.points[0].x;
        float min_y = path_.points[0].y;
        float max_y = path_.points[0].y;
        for (const auto& pt : path_.points) {
            min_x = std::min(min_x, pt.x);
            max_x = std::max(max_x, pt.x);
            min_y = std::min(min_y, pt.y);
            max_y = std::max(max_y, pt.y);
        }
        min_x -= thickness / 2.0F;
        max_x += thickness / 2.0F;
        min_y -= thickness / 2.0F;
        max_y += thickness / 2.0F;
        set_bounds({min_x, min_y, max_x - min_x, max_y - min_y});
    }
}

const Path2D& PathNode::path() const {
    return path_;
}

Color PathNode::stroke_color() const {
    return stroke_color_;
}

float PathNode::thickness() const {
    return thickness_;
}

// --- TransformNode ---

TransformNode::TransformNode(Matrix3x2 transform)
    : RenderNode(RenderNodeKind::Transform), transform_(transform) {
    // TransformNode bounds are unbounded by default or derived from children.
}

const Matrix3x2& TransformNode::transform() const {
    return transform_;
}

// --- TextNode ---

TextNode::TextNode(
    Point origin, Size size, std::string text, Color color, FontDescriptor font, float max_width)
    : RenderNode(RenderNodeKind::Text)
    , text_(std::move(text))
    , color_(color)
    , font_(std::move(font))
    , max_width_(max_width) {
    set_bounds({origin.x, origin.y, size.width, size.height});
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

float TextNode::max_width() const {
    return max_width_;
}

// --- OpacityNode ---

OpacityNode::OpacityNode(Rect bounds, float opacity)
    : RenderNode(RenderNodeKind::Opacity), opacity_(std::clamp(opacity, 0.0F, 1.0F)) {
    set_bounds(bounds);
}

float OpacityNode::opacity() const {
    return opacity_;
}

// --- LinearGradientNode ---

LinearGradientNode::LinearGradientNode(Rect bounds,
                                       Color start_color,
                                       Color end_color,
                                       Orientation direction)
    : RenderNode(RenderNodeKind::LinearGradient)
    , start_color_(start_color)
    , end_color_(end_color)
    , direction_(direction) {
    set_bounds(bounds);
}

Color LinearGradientNode::start_color() const {
    return start_color_;
}

Color LinearGradientNode::end_color() const {
    return end_color_;
}

Orientation LinearGradientNode::direction() const {
    return direction_;
}

// --- ShadowNode ---

ShadowNode::ShadowNode(Rect rect,
                       Color color,
                       float offset_x,
                       float offset_y,
                       float blur_radius,
                       float spread,
                       float corner_radius)
    : RenderNode(RenderNodeKind::Shadow)
    , color_(color)
    , offset_x_(offset_x)
    , offset_y_(offset_y)
    , blur_radius_(std::max(0.0F, blur_radius))
    , spread_(spread)
    , corner_radius_(std::max(0.0F, corner_radius)) {
    // Bounds cover the full shadow extent: the element rect + offset + blur + spread.
    const float extend = blur_radius_ + spread_;
    set_bounds({rect.x + offset_x_ - extend,
                rect.y + offset_y_ - extend,
                rect.width + extend * 2.0F,
                rect.height + extend * 2.0F});
}

Color ShadowNode::color() const {
    return color_;
}

float ShadowNode::offset_x() const {
    return offset_x_;
}

float ShadowNode::offset_y() const {
    return offset_y_;
}

float ShadowNode::blur_radius() const {
    return blur_radius_;
}

float ShadowNode::spread() const {
    return spread_;
}

float ShadowNode::corner_radius() const {
    return corner_radius_;
}

} // namespace nk
