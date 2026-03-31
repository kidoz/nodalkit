#include <algorithm>
#include <nk/platform/events.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/scroll_area.h>

namespace nk {

namespace {

float max_h_offset(const Widget* content, Rect viewport);
float max_v_offset(const Widget* content, Rect viewport);

} // namespace

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

ScrollArea::ScrollArea() : impl_(std::make_unique<Impl>()) {
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

float ScrollArea::h_offset() const {
    return impl_->h_offset;
}

float ScrollArea::v_offset() const {
    return impl_->v_offset;
}

void ScrollArea::scroll_to(float h, float v) {
    const auto viewport = allocation();
    const float next_h = std::clamp(h, 0.0F, max_h_offset(impl_->content.get(), viewport));
    const float next_v = std::clamp(v, 0.0F, max_v_offset(impl_->content.get(), viewport));
    impl_->h_offset = next_h;
    impl_->v_offset = next_v;
    impl_->scroll_changed.emit(next_h, next_v);
    queue_redraw();
}

Signal<float, float>& ScrollArea::on_scroll_changed() {
    return impl_->scroll_changed;
}

namespace {

float max_h_offset(const Widget* content, Rect viewport) {
    if (content == nullptr) {
        return 0.0F;
    }
    const auto req = content->measure(Constraints::unbounded());
    return std::max(0.0F, req.natural_width - viewport.width);
}

float max_v_offset(const Widget* content, Rect viewport) {
    if (content == nullptr) {
        return 0.0F;
    }
    const auto req = content->measure(Constraints::unbounded());
    return std::max(0.0F, req.natural_height - viewport.height);
}

Rect scrollbar_track(Rect viewport, bool vertical) {
    constexpr float thickness = 10.0F;
    if (vertical) {
        return {viewport.right() - thickness,
                viewport.y + 4.0F,
                thickness,
                std::max(0.0F, viewport.height - 8.0F)};
    }
    return {viewport.x + 4.0F,
            viewport.bottom() - thickness,
            std::max(0.0F, viewport.width - 8.0F),
            thickness};
}

} // namespace

SizeRequest ScrollArea::measure(const Constraints& constraints) const {
    if (impl_->content) {
        return impl_->content->measure(constraints);
    }
    return {};
}

void ScrollArea::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    impl_->h_offset =
        std::clamp(impl_->h_offset, 0.0F, max_h_offset(impl_->content.get(), allocation));
    impl_->v_offset =
        std::clamp(impl_->v_offset, 0.0F, max_v_offset(impl_->content.get(), allocation));
    if (impl_->content) {
        // Content can be larger than the viewport.
        const auto req = impl_->content->measure(Constraints::unbounded());
        const float cw = std::max(allocation.width, req.natural_width);
        const float ch = std::max(allocation.height, req.natural_height);
        impl_->content->allocate({-impl_->h_offset, -impl_->v_offset, cw, ch});
    }
}

bool ScrollArea::handle_mouse_event(const MouseEvent& event) {
    if (event.type != MouseEvent::Type::Scroll || !allocation().contains({event.x, event.y})) {
        return false;
    }

    constexpr float scroll_step = 40.0F;
    float next_h = impl_->h_offset;
    float next_v = impl_->v_offset;

    if (impl_->h_policy != ScrollPolicy::Never) {
        next_h -= event.scroll_dx * scroll_step;
    }
    if (impl_->v_policy != ScrollPolicy::Never) {
        next_v -= event.scroll_dy * scroll_step;
    }

    scroll_to(next_h, next_v);
    return true;
}

void ScrollArea::snapshot(SnapshotContext& ctx) const {
    const auto viewport = allocation();
    const float corner_radius = theme_number("corner-radius", 12.0F);

    ctx.add_rounded_rect(
        viewport, theme_color("background", Color{1.0F, 1.0F, 1.0F, 0.0F}), corner_radius);

    ctx.push_rounded_clip(viewport, corner_radius);
    Widget::snapshot(ctx);
    ctx.pop_container();

    if (!impl_->content) {
        return;
    }

    const auto req = impl_->content->measure(Constraints::unbounded());
    const bool show_v_scrollbar =
        impl_->v_policy == ScrollPolicy::Always ||
        (impl_->v_policy == ScrollPolicy::Automatic && req.natural_height > viewport.height);
    const bool show_h_scrollbar =
        impl_->h_policy == ScrollPolicy::Always ||
        (impl_->h_policy == ScrollPolicy::Automatic && req.natural_width > viewport.width);

    const auto track_color =
        theme_color("scrollbar-track-color", Color{0.16F, 0.19F, 0.23F, 0.06F});
    const auto thumb_color =
        theme_color("scrollbar-thumb-color", Color{0.16F, 0.19F, 0.23F, 0.22F});

    if (show_v_scrollbar) {
        const auto track = scrollbar_track(viewport, true);
        const float max_offset = std::max(1.0F, req.natural_height - viewport.height);
        const float thumb_height =
            std::max(28.0F, (viewport.height / req.natural_height) * track.height);
        const float thumb_y =
            track.y + (impl_->v_offset / max_offset) * std::max(0.0F, track.height - thumb_height);
        ctx.add_rounded_rect(track, track_color, track.width * 0.5F);
        ctx.add_rounded_rect({track.x + 2.0F, thumb_y, track.width - 4.0F, thumb_height},
                             thumb_color,
                             (track.width - 4.0F) * 0.5F);
    }

    if (show_h_scrollbar) {
        const auto track = scrollbar_track(viewport, false);
        const float max_offset = std::max(1.0F, req.natural_width - viewport.width);
        const float thumb_width =
            std::max(28.0F, (viewport.width / req.natural_width) * track.width);
        const float thumb_x =
            track.x + (impl_->h_offset / max_offset) * std::max(0.0F, track.width - thumb_width);
        ctx.add_rounded_rect(track, track_color, track.height * 0.5F);
        ctx.add_rounded_rect({thumb_x, track.y + 2.0F, thumb_width, track.height - 4.0F},
                             thumb_color,
                             (track.height - 4.0F) * 0.5F);
    }
}

} // namespace nk
