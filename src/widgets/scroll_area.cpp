#include <algorithm>
#include <nk/platform/events.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/scroll_area.h>

namespace nk {

namespace {

Constraints content_constraints(Rect viewport, ScrollPolicy h_policy, ScrollPolicy v_policy);
SizeRequest
content_request(const Widget* content, Rect viewport, ScrollPolicy h_policy, ScrollPolicy v_policy);
float max_h_offset(const Widget* content,
                   Rect viewport,
                   ScrollPolicy h_policy,
                   ScrollPolicy v_policy);
float max_v_offset(const Widget* content,
                   Rect viewport,
                   ScrollPolicy h_policy,
                   ScrollPolicy v_policy);
bool rect_is_empty(Rect rect);
bool should_show_v_scrollbar(ScrollPolicy h_policy,
                             ScrollPolicy v_policy,
                             const Widget* content,
                             Rect viewport);
bool should_show_h_scrollbar(ScrollPolicy h_policy,
                             ScrollPolicy v_policy,
                             const Widget* content,
                             Rect viewport);
Rect v_thumb_rect(ScrollPolicy h_policy,
                  ScrollPolicy v_policy,
                  const Widget* content,
                  Rect viewport,
                  float v_offset);
Rect h_thumb_rect(ScrollPolicy h_policy,
                  ScrollPolicy v_policy,
                  const Widget* content,
                  Rect viewport,
                  float h_offset);

} // namespace

struct ScrollArea::Impl {
    std::shared_ptr<Widget> content;
    ScrollPolicy h_policy = ScrollPolicy::Automatic;
    ScrollPolicy v_policy = ScrollPolicy::Automatic;
    float h_offset = 0;
    float v_offset = 0;
    bool dragging_h_thumb = false;
    bool dragging_v_thumb = false;
    float drag_origin_x = 0;
    float drag_origin_y = 0;
    float drag_start_h_offset = 0;
    float drag_start_v_offset = 0;
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
    const float next_h = std::clamp(
        h, 0.0F, max_h_offset(impl_->content.get(), viewport, impl_->h_policy, impl_->v_policy));
    const float next_v = std::clamp(
        v, 0.0F, max_v_offset(impl_->content.get(), viewport, impl_->h_policy, impl_->v_policy));
    if (impl_->h_offset == next_h && impl_->v_offset == next_v) {
        return;
    }
    impl_->h_offset = next_h;
    impl_->v_offset = next_v;
    impl_->scroll_changed.emit(next_h, next_v);
    queue_layout();
    queue_redraw();
}

Signal<float, float>& ScrollArea::on_scroll_changed() {
    return impl_->scroll_changed;
}

namespace {

Constraints content_constraints(Rect viewport, ScrollPolicy h_policy, ScrollPolicy v_policy) {
    Constraints constraints = Constraints::unbounded();
    if (h_policy == ScrollPolicy::Never) {
        constraints.max_width = viewport.width;
    }
    if (v_policy == ScrollPolicy::Never) {
        constraints.max_height = viewport.height;
    }
    return constraints;
}

SizeRequest content_request(const Widget* content,
                            Rect viewport,
                            ScrollPolicy h_policy,
                            ScrollPolicy v_policy) {
    if (content == nullptr) {
        return {};
    }
    return content->measure_for_diagnostics(content_constraints(viewport, h_policy, v_policy));
}

float max_h_offset(const Widget* content,
                   Rect viewport,
                   ScrollPolicy h_policy,
                   ScrollPolicy v_policy) {
    if (h_policy == ScrollPolicy::Never) {
        return 0.0F;
    }
    if (content == nullptr) {
        return 0.0F;
    }
    const auto req = content_request(content, viewport, h_policy, v_policy);
    return std::max(0.0F, req.natural_width - viewport.width);
}

float max_v_offset(const Widget* content,
                   Rect viewport,
                   ScrollPolicy h_policy,
                   ScrollPolicy v_policy) {
    if (v_policy == ScrollPolicy::Never) {
        return 0.0F;
    }
    if (content == nullptr) {
        return 0.0F;
    }
    const auto req = content_request(content, viewport, h_policy, v_policy);
    return std::max(0.0F, req.natural_height - viewport.height);
}

bool rect_is_empty(Rect rect) {
    return rect.width <= 0.0F || rect.height <= 0.0F;
}

Rect scrollbar_track(Rect viewport, bool vertical) {
    constexpr float thickness = 11.0F;
    constexpr float edge_inset = 6.0F;
    if (vertical) {
        return {viewport.right() - thickness - edge_inset,
                viewport.y + 6.0F,
                thickness,
                std::max(0.0F, viewport.height - 12.0F)};
    }
    return {viewport.x + 6.0F,
            viewport.bottom() - thickness - edge_inset,
            std::max(0.0F, viewport.width - 12.0F),
            thickness};
}

bool should_show_v_scrollbar(ScrollPolicy h_policy,
                             ScrollPolicy v_policy,
                             const Widget* content,
                             Rect viewport) {
    if (content == nullptr) {
        return false;
    }
    if (v_policy == ScrollPolicy::Always) {
        return true;
    }
    if (v_policy == ScrollPolicy::Never) {
        return false;
    }
    const auto req = content_request(content, viewport, h_policy, v_policy);
    return req.natural_height > viewport.height;
}

bool should_show_h_scrollbar(ScrollPolicy h_policy,
                             ScrollPolicy v_policy,
                             const Widget* content,
                             Rect viewport) {
    if (content == nullptr) {
        return false;
    }
    if (h_policy == ScrollPolicy::Always) {
        return true;
    }
    if (h_policy == ScrollPolicy::Never) {
        return false;
    }
    const auto req = content_request(content, viewport, h_policy, v_policy);
    return req.natural_width > viewport.width;
}

Rect v_thumb_rect(ScrollPolicy h_policy,
                  ScrollPolicy v_policy,
                  const Widget* content,
                  Rect viewport,
                  float v_offset) {
    if (!should_show_v_scrollbar(h_policy, v_policy, content, viewport)) {
        return {};
    }
    const auto req = content_request(content, viewport, h_policy, v_policy);
    const auto track = scrollbar_track(viewport, true);
    const float max_offset = std::max(1.0F, req.natural_height - viewport.height);
    const float thumb_height =
        std::max(28.0F, (viewport.height / req.natural_height) * track.height);
    const float thumb_y =
        track.y + (v_offset / max_offset) * std::max(0.0F, track.height - thumb_height);
    return {track.x + 2.0F, thumb_y, track.width - 4.0F, thumb_height};
}

Rect h_thumb_rect(ScrollPolicy h_policy,
                  ScrollPolicy v_policy,
                  const Widget* content,
                  Rect viewport,
                  float h_offset) {
    if (!should_show_h_scrollbar(h_policy, v_policy, content, viewport)) {
        return {};
    }
    const auto req = content_request(content, viewport, h_policy, v_policy);
    const auto track = scrollbar_track(viewport, false);
    const float max_offset = std::max(1.0F, req.natural_width - viewport.width);
    const float thumb_width = std::max(28.0F, (viewport.width / req.natural_width) * track.width);
    const float thumb_x =
        track.x + (h_offset / max_offset) * std::max(0.0F, track.width - thumb_width);
    return {thumb_x, track.y + 2.0F, thumb_width, track.height - 4.0F};
}

} // namespace

SizeRequest ScrollArea::measure(const Constraints& constraints) const {
    if (impl_->content) {
        return impl_->content->measure_for_diagnostics(constraints);
    }
    return {};
}

void ScrollArea::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    impl_->h_offset = std::clamp(
        impl_->h_offset,
        0.0F,
        max_h_offset(impl_->content.get(), allocation, impl_->h_policy, impl_->v_policy));
    impl_->v_offset = std::clamp(
        impl_->v_offset,
        0.0F,
        max_v_offset(impl_->content.get(), allocation, impl_->h_policy, impl_->v_policy));
    if (impl_->content) {
        const auto req =
            content_request(impl_->content.get(), allocation, impl_->h_policy, impl_->v_policy);
        const float cw = impl_->h_policy == ScrollPolicy::Never
                             ? allocation.width
                             : std::max(allocation.width, req.natural_width);
        const float ch = impl_->v_policy == ScrollPolicy::Never
                             ? allocation.height
                             : std::max(allocation.height, req.natural_height);
        impl_->content->allocate(
            {allocation.x - impl_->h_offset, allocation.y - impl_->v_offset, cw, ch});
    }
}

bool ScrollArea::handle_mouse_event(const MouseEvent& event) {
    const auto viewport = allocation();
    const Point point{event.x, event.y};
    if (!viewport.contains(point)) {
        if (event.type == MouseEvent::Type::Release) {
            impl_->dragging_h_thumb = false;
            impl_->dragging_v_thumb = false;
        }
        return false;
    }

    const auto v_track = scrollbar_track(viewport, true);
    const auto h_track = scrollbar_track(viewport, false);
    const auto v_thumb = v_thumb_rect(
        impl_->h_policy, impl_->v_policy, impl_->content.get(), viewport, impl_->v_offset);
    const auto h_thumb = h_thumb_rect(
        impl_->h_policy, impl_->v_policy, impl_->content.get(), viewport, impl_->h_offset);

    switch (event.type) {
    case MouseEvent::Type::Scroll: {
        const float scroll_step = event.precise_scrolling ? 1.0F : 40.0F;
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
    case MouseEvent::Type::Press: {
        if (event.button != 1) {
            return false;
        }
        if (v_thumb.contains(point)) {
            impl_->dragging_v_thumb = true;
            impl_->drag_origin_y = point.y;
            impl_->drag_start_v_offset = impl_->v_offset;
            return true;
        }
        if (h_thumb.contains(point)) {
            impl_->dragging_h_thumb = true;
            impl_->drag_origin_x = point.x;
            impl_->drag_start_h_offset = impl_->h_offset;
            return true;
        }
        if (v_track.contains(point) && !rect_is_empty(v_thumb)) {
            const auto req =
                content_request(impl_->content.get(), viewport, impl_->h_policy, impl_->v_policy);
            const float max_offset = std::max(1.0F, req.natural_height - viewport.height);
            const float travel = std::max(1.0F, v_track.height - v_thumb.height);
            const float thumb_top = std::clamp(
                point.y - (v_thumb.height * 0.5F), v_track.y, v_track.bottom() - v_thumb.height);
            const float ratio = (thumb_top - v_track.y) / travel;
            scroll_to(impl_->h_offset, ratio * max_offset);
            impl_->dragging_v_thumb = true;
            impl_->drag_origin_y = point.y;
            impl_->drag_start_v_offset = impl_->v_offset;
            return true;
        }
        if (h_track.contains(point) && !rect_is_empty(h_thumb)) {
            const auto req =
                content_request(impl_->content.get(), viewport, impl_->h_policy, impl_->v_policy);
            const float max_offset = std::max(1.0F, req.natural_width - viewport.width);
            const float travel = std::max(1.0F, h_track.width - h_thumb.width);
            const float thumb_left = std::clamp(
                point.x - (h_thumb.width * 0.5F), h_track.x, h_track.right() - h_thumb.width);
            const float ratio = (thumb_left - h_track.x) / travel;
            scroll_to(ratio * max_offset, impl_->v_offset);
            impl_->dragging_h_thumb = true;
            impl_->drag_origin_x = point.x;
            impl_->drag_start_h_offset = impl_->h_offset;
            return true;
        }
        return false;
    }
    case MouseEvent::Type::Move: {
        if (impl_->dragging_v_thumb && !rect_is_empty(v_thumb)) {
            const auto req =
                content_request(impl_->content.get(), viewport, impl_->h_policy, impl_->v_policy);
            const float max_offset = std::max(1.0F, req.natural_height - viewport.height);
            const float travel = std::max(1.0F, v_track.height - v_thumb.height);
            const float delta = point.y - impl_->drag_origin_y;
            const float offset_delta = (delta / travel) * max_offset;
            scroll_to(impl_->h_offset, impl_->drag_start_v_offset + offset_delta);
            return true;
        }
        if (impl_->dragging_h_thumb && !rect_is_empty(h_thumb)) {
            const auto req =
                content_request(impl_->content.get(), viewport, impl_->h_policy, impl_->v_policy);
            const float max_offset = std::max(1.0F, req.natural_width - viewport.width);
            const float travel = std::max(1.0F, h_track.width - h_thumb.width);
            const float delta = point.x - impl_->drag_origin_x;
            const float offset_delta = (delta / travel) * max_offset;
            scroll_to(impl_->drag_start_h_offset + offset_delta, impl_->v_offset);
            return true;
        }
        return false;
    }
    case MouseEvent::Type::Release:
        impl_->dragging_h_thumb = false;
        impl_->dragging_v_thumb = false;
        return event.button == 1;
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
        return false;
    }

    return false;
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

    const bool show_v_scrollbar =
        should_show_v_scrollbar(impl_->h_policy, impl_->v_policy, impl_->content.get(), viewport);
    const bool show_h_scrollbar =
        should_show_h_scrollbar(impl_->h_policy, impl_->v_policy, impl_->content.get(), viewport);

    const auto raw_track = theme_color("scrollbar-track-color", Color{0.88F, 0.90F, 0.93F, 1.0F});
    const auto raw_thumb = theme_color("scrollbar-thumb-color", Color{0.67F, 0.71F, 0.76F, 1.0F});
    const auto track_color = Color{raw_track.r, raw_track.g, raw_track.b, 0.72F};
    const auto thumb_color = Color{raw_thumb.r, raw_thumb.g, raw_thumb.b, 0.86F};

    if (show_v_scrollbar) {
        const auto track = scrollbar_track(viewport, true);
        const auto thumb = v_thumb_rect(
            impl_->h_policy, impl_->v_policy, impl_->content.get(), viewport, impl_->v_offset);
        ctx.add_rounded_rect(track, track_color, track.width * 0.5F);
        ctx.add_rounded_rect(thumb, thumb_color, thumb.width * 0.5F);
    }

    if (show_h_scrollbar) {
        const auto track = scrollbar_track(viewport, false);
        const auto thumb = h_thumb_rect(
            impl_->h_policy, impl_->v_policy, impl_->content.get(), viewport, impl_->h_offset);
        ctx.add_rounded_rect(track, track_color, track.height * 0.5F);
        ctx.add_rounded_rect(thumb, thumb_color, thumb.height * 0.5F);
    }
}

} // namespace nk
