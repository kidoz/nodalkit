#include <nk/widgets/scroll_area.h>

#include <nk/render/snapshot_context.h>

namespace nk {

struct ScrollArea::Impl {
    std::shared_ptr<Widget> content;
    ScrollPolicy h_policy = ScrollPolicy::Automatic;
    ScrollPolicy v_policy = ScrollPolicy::Automatic;
    float h_offset = 0;
    float v_offset = 0;
    Signal<float, float> scroll_changed;
};

std::shared_ptr<ScrollArea> ScrollArea::create() {
    return std::shared_ptr<ScrollArea>(new ScrollArea());
}

ScrollArea::ScrollArea()
    : impl_(std::make_unique<Impl>()) {
    add_style_class("scroll-area");
}

ScrollArea::~ScrollArea() = default;

void ScrollArea::set_content(std::shared_ptr<Widget> content) {
    if (impl_->content) {
        remove_child(*impl_->content);
    }
    impl_->content = std::move(content);
    if (impl_->content) {
        append_child(impl_->content);
    }
    queue_layout();
}

Widget* ScrollArea::content() const {
    return impl_->content.get();
}

void ScrollArea::set_h_scroll_policy(ScrollPolicy policy) {
    impl_->h_policy = policy;
}

void ScrollArea::set_v_scroll_policy(ScrollPolicy policy) {
    impl_->v_policy = policy;
}

float ScrollArea::h_offset() const { return impl_->h_offset; }
float ScrollArea::v_offset() const { return impl_->v_offset; }

void ScrollArea::scroll_to(float h, float v) {
    impl_->h_offset = h;
    impl_->v_offset = v;
    impl_->scroll_changed.emit(h, v);
    queue_redraw();
}

Signal<float, float>& ScrollArea::on_scroll_changed() {
    return impl_->scroll_changed;
}

SizeRequest ScrollArea::measure(Constraints const& constraints) const {
    if (impl_->content) {
        return impl_->content->measure(constraints);
    }
    return {};
}

void ScrollArea::allocate(Rect const& allocation) {
    Widget::allocate(allocation);
    if (impl_->content) {
        // Content can be larger than the viewport.
        auto const req = impl_->content->measure(Constraints::unbounded());
        float const cw = std::max(allocation.width, req.natural_width);
        float const ch = std::max(allocation.height, req.natural_height);
        impl_->content->allocate(
            {-impl_->h_offset, -impl_->v_offset, cw, ch});
    }
}

void ScrollArea::snapshot(SnapshotContext& ctx) const {
    // Delegate to base Widget to recursively snapshot children.
    Widget::snapshot(ctx);
}

} // namespace nk
