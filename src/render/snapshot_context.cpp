#include <nk/render/snapshot_context.h>

#include <cassert>
#include <stack>
#include <utility>

namespace nk {

struct SnapshotContext::Impl {
    /// The root container node that collects all top-level children.
    std::unique_ptr<RenderNode> root;
    RenderNode* content_root = nullptr;
    RenderNode* overlay_root = nullptr;

    /// Stack of container nodes. The top of the stack is the current
    /// target for add_node(). We store raw pointers because the
    /// owning unique_ptr lives in the parent's children list (or in
    /// `root` for the bottom-most entry).
    std::stack<RenderNode*> container_stack;
};

SnapshotContext::SnapshotContext()
    : impl_(std::make_unique<Impl>()) {
    // Create the implicit root container.
    impl_->root = std::make_unique<RenderNode>(RenderNodeKind::Container);

    auto content = std::make_unique<RenderNode>(RenderNodeKind::Container);
    impl_->content_root = content.get();
    impl_->root->append_child(std::move(content));

    auto overlay = std::make_unique<RenderNode>(RenderNodeKind::Container);
    impl_->overlay_root = overlay.get();
    impl_->root->append_child(std::move(overlay));

    impl_->container_stack.push(impl_->content_root);
}

SnapshotContext::~SnapshotContext() = default;

void SnapshotContext::add_node(std::unique_ptr<RenderNode> node) {
    assert(!impl_->container_stack.empty());
    impl_->container_stack.top()->append_child(std::move(node));
}

void SnapshotContext::add_color_rect(Rect rect, Color color) {
    add_node(std::make_unique<ColorRectNode>(rect, color));
}

void SnapshotContext::add_rounded_rect(
    Rect rect,
    Color color,
    float corner_radius) {
    add_node(std::make_unique<RoundedRectNode>(rect, color, corner_radius));
}

void SnapshotContext::add_border(
    Rect rect,
    Color color,
    float thickness,
    float corner_radius) {
    add_node(std::make_unique<BorderNode>(
        rect, color, thickness, corner_radius));
}

void SnapshotContext::add_text(
    Point origin,
    std::string text,
    Color color,
    FontDescriptor font) {
    add_node(std::make_unique<TextNode>(
        origin, std::move(text), color, std::move(font)));
}

void SnapshotContext::add_image(Rect dest, uint32_t const* data, int w, int h,
                                ScaleMode scale) {
    add_node(std::make_unique<ImageNode>(dest, data, w, h, scale));
}

void SnapshotContext::push_container(Rect bounds) {
    auto container = std::make_unique<RenderNode>(RenderNodeKind::Container);
    container->set_bounds(bounds);
    auto* raw = container.get();
    add_node(std::move(container));
    impl_->container_stack.push(raw);
}

void SnapshotContext::push_rounded_clip(Rect bounds, float corner_radius) {
    auto clip = std::make_unique<RoundedClipNode>(bounds, corner_radius);
    auto* raw = clip.get();
    add_node(std::move(clip));
    impl_->container_stack.push(raw);
}

void SnapshotContext::push_overlay_container(Rect bounds) {
    auto container = std::make_unique<RenderNode>(RenderNodeKind::Container);
    container->set_bounds(bounds);
    auto* raw = container.get();
    impl_->overlay_root->append_child(std::move(container));
    impl_->container_stack.push(raw);
}

void SnapshotContext::pop_container() {
    assert(impl_->container_stack.size() > 1 &&
           "Cannot pop the root container");
    impl_->container_stack.pop();
}

std::unique_ptr<RenderNode> SnapshotContext::take_root() {
    impl_->container_stack = {};
    return std::move(impl_->root);
}

} // namespace nk
