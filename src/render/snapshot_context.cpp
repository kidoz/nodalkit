#include <cassert>
#include <nk/render/snapshot_context.h>
#include <nk/text/text_shaper.h>
#include <stack>
#include <utility>

namespace nk {

struct SnapshotContext::Impl {
    struct DebugSourceScope {
        std::string label;
        std::vector<std::size_t> path;
    };

    /// The root container node that collects all top-level children.
    std::unique_ptr<RenderNode> root;
    RenderNode* content_root = nullptr;
    RenderNode* overlay_root = nullptr;

    /// Stack of container nodes. The top of the stack is the current
    /// target for add_node(). We store raw pointers because the
    /// owning unique_ptr lives in the parent's children list (or in
    /// `root` for the bottom-most entry).
    std::stack<RenderNode*> container_stack;
    std::vector<DebugSourceScope> debug_source_stack;
    bool debug_annotations_enabled = true;
    TextShaper* text_shaper = nullptr;
};

SnapshotContext::SnapshotContext(TextShaper* text_shaper) : impl_(std::make_unique<Impl>()) {
    impl_->text_shaper = text_shaper;

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
    if (!impl_->debug_source_stack.empty()) {
        const auto& source = impl_->debug_source_stack.back();
        node->set_debug_source(source.label, source.path);
    }
    impl_->container_stack.top()->append_child(std::move(node));
}

void SnapshotContext::add_color_rect(Rect rect, Color color) {
    add_node(std::make_unique<ColorRectNode>(rect, color));
}

void SnapshotContext::add_rounded_rect(Rect rect, Color color, float corner_radius) {
    add_node(std::make_unique<RoundedRectNode>(rect, color, corner_radius));
}

void SnapshotContext::add_border(Rect rect, Color color, float thickness, float corner_radius) {
    add_node(std::make_unique<BorderNode>(rect, color, thickness, corner_radius));
}

void SnapshotContext::add_text(Point origin, std::string text, Color color, FontDescriptor font) {
    Size measured{};
    if (impl_->text_shaper != nullptr) {
        measured = impl_->text_shaper->measure(text, font);
    }
    add_node(std::make_unique<TextNode>(origin, measured, std::move(text), color, std::move(font)));
}

void SnapshotContext::add_wrapped_text(Point origin, std::string text, Color color,
                                        FontDescriptor font, float max_width) {
    Size measured{};
    if (impl_->text_shaper != nullptr) {
        measured = impl_->text_shaper->measure_wrapped(text, font, max_width);
    }
    add_node(std::make_unique<TextNode>(origin, measured, std::move(text), color,
                                         std::move(font), max_width));
}

void SnapshotContext::add_image(Rect dest, const uint32_t* data, int w, int h, ScaleMode scale) {
    add_node(std::make_unique<ImageNode>(dest, data, w, h, scale));
}

void SnapshotContext::push_container(Rect bounds) {
    auto container = std::make_unique<RenderNode>(RenderNodeKind::Container);
    container->set_bounds(bounds);
    auto* raw = container.get();
    add_node(std::move(container));
    impl_->container_stack.push(raw);
}

void SnapshotContext::set_debug_annotations_enabled(bool enabled) {
    impl_->debug_annotations_enabled = enabled;
}

bool SnapshotContext::debug_annotations_enabled() const {
    return impl_->debug_annotations_enabled;
}

void SnapshotContext::push_debug_source(std::string label, std::span<const std::size_t> path) {
    if (!impl_->debug_annotations_enabled) {
        return;
    }
    impl_->debug_source_stack.push_back(
        {std::move(label), std::vector<std::size_t>(path.begin(), path.end())});
}

void SnapshotContext::pop_debug_source() {
    if (!impl_->debug_annotations_enabled) {
        return;
    }
    assert(!impl_->debug_source_stack.empty() && "Cannot pop an empty debug source stack");
    impl_->debug_source_stack.pop_back();
}

void SnapshotContext::push_rounded_clip(Rect bounds, float corner_radius) {
    auto clip = std::make_unique<RoundedClipNode>(bounds, corner_radius);
    auto* raw = clip.get();
    add_node(std::move(clip));
    impl_->container_stack.push(raw);
}

void SnapshotContext::add_linear_gradient(Rect bounds, Color start_color, Color end_color,
                                          Orientation direction) {
    add_node(std::make_unique<LinearGradientNode>(bounds, start_color, end_color, direction));
}

void SnapshotContext::add_shadow(Rect rect, Color color, float offset_x, float offset_y,
                                 float blur_radius, float spread, float corner_radius) {
    add_node(std::make_unique<ShadowNode>(rect, color, offset_x, offset_y, blur_radius, spread,
                                          corner_radius));
}

void SnapshotContext::push_opacity(Rect bounds, float opacity) {
    auto node = std::make_unique<OpacityNode>(bounds, opacity);
    auto* raw = node.get();
    add_node(std::move(node));
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
    assert(impl_->container_stack.size() > 1 && "Cannot pop the root container");
    impl_->container_stack.pop();
}

std::unique_ptr<RenderNode> SnapshotContext::take_root() {
    impl_->container_stack = {};
    return std::move(impl_->root);
}

} // namespace nk
