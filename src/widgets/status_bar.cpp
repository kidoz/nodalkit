#include <algorithm>
#include <cmath>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/status_bar.h>
#include <stdexcept>

namespace nk {

namespace {

FontDescriptor status_font() {
    return FontDescriptor{
        .family = {},
        .size = 12.5F,
        .weight = FontWeight::Regular,
    };
}

std::string join_segments(const std::vector<std::string>& segments) {
    std::string summary;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index > 0) {
            summary += " | ";
        }
        summary += segments[index];
    }
    return summary;
}

std::string status_description(const std::vector<std::string>& segments,
                               const std::string& trailing) {
    std::string summary = join_segments(segments);
    if (!trailing.empty()) {
        if (!summary.empty()) {
            summary += " | ";
        }
        summary += trailing;
    }
    return summary;
}

Rect union_rect(Rect lhs, Rect rhs) {
    if (lhs.width <= 0.0F || lhs.height <= 0.0F) {
        return rhs;
    }
    if (rhs.width <= 0.0F || rhs.height <= 0.0F) {
        return lhs;
    }
    const float left = std::min(lhs.x, rhs.x);
    const float top = std::min(lhs.y, rhs.y);
    const float right = std::max(lhs.right(), rhs.right());
    const float bottom = std::max(lhs.bottom(), rhs.bottom());
    return {left, top, right - left, bottom - top};
}

template <typename MeasureTextFn>
Rect status_tail_damage_rect(float bar_width,
                             float bar_height,
                             const std::vector<std::string>& segments,
                             std::size_t start_index,
                             float gap,
                             MeasureTextFn&& measure_text_fn) {
    if (bar_width <= 0.0F || bar_height <= 0.0F || start_index >= segments.size()) {
        return {0.0F, 0.0F, bar_width, bar_height};
    }

    float x_offset = 16.0F;
    float start_x = x_offset;
    float end_x = start_x;
    bool found_start = false;

    for (std::size_t index = 0; index < segments.size(); ++index) {
        const auto measured = measure_text_fn(segments[index]);
        if (index == start_index) {
            start_x = x_offset;
            found_start = true;
        }
        if (found_start) {
            end_x = std::max(end_x, x_offset + measured.width);
        }
        x_offset += measured.width + gap;
        if (found_start && index + 1 < segments.size()) {
            end_x = std::max(end_x, x_offset - (gap * 0.5F) + 1.0F);
        }
    }

    if (!found_start) {
        return {};
    }
    return {
        start_x,
        0.0F,
        std::max(0.0F, std::min(end_x, bar_width) - start_x),
        bar_height,
    };
}

template <typename MeasureTextFn>
Rect trailing_damage_rect(float bar_width,
                          float bar_height,
                          const std::string& trailing,
                          MeasureTextFn&& measure_text_fn) {
    if (bar_width <= 0.0F || bar_height <= 0.0F || trailing.empty()) {
        return {0.0F, 0.0F, bar_width, bar_height};
    }
    const auto measured = measure_text_fn(trailing);
    const float right_edge = std::max(0.0F, bar_width - 16.0F);
    const float left = std::max(0.0F, right_edge - measured.width);
    return {left, 0.0F, std::max(0.0F, right_edge - left), bar_height};
}

constexpr float kGripSize = 12.0F;

} // namespace

struct StatusBar::Impl {
    std::vector<std::string> segments;
    std::vector<std::shared_ptr<Widget>> widgets;
    std::string trailing;
    bool grip_armed = false;
    bool grip_hovered = false;
    Point grip_press{};
    Size grip_start_size{};
};

std::shared_ptr<StatusBar> StatusBar::create() {
    return std::shared_ptr<StatusBar>(new StatusBar());
}

StatusBar::StatusBar() : impl_(std::make_unique<Impl>()) {
    add_style_class("status-bar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Status);
    accessible.set_name("status bar");
}

StatusBar::~StatusBar() = default;

void StatusBar::sync_widget_slots() {
    if (impl_->widgets.size() > impl_->segments.size()) {
        for (std::size_t index = impl_->segments.size(); index < impl_->widgets.size(); ++index) {
            if (impl_->widgets[index] != nullptr) {
                remove_child(*impl_->widgets[index]);
            }
        }
    }
    impl_->widgets.resize(impl_->segments.size());
}

void StatusBar::set_segments(std::vector<std::string> segments) {
    const auto previous_segments = impl_->segments;
    impl_->segments = std::move(segments);
    sync_widget_slots();
    ensure_accessible().set_description(status_description(impl_->segments, impl_->trailing));
    const auto a = allocation();
    if (previous_segments.size() != impl_->segments.size() || a.width <= 0.0F || a.height <= 0.0F) {
        queue_layout();
        queue_redraw();
        return;
    }

    const auto mismatch = std::mismatch(previous_segments.begin(),
                                        previous_segments.end(),
                                        impl_->segments.begin(),
                                        impl_->segments.end());
    if (mismatch.first == previous_segments.end()) {
        return;
    }

    const auto changed_index =
        static_cast<std::size_t>(std::distance(previous_segments.begin(), mismatch.first));
    const float gap = theme_number("segment-gap", 16.0F);
    const auto font = status_font();
    const auto measure_segment = [&](std::string_view text) { return measure_text(text, font); };
    const auto previous_damage = status_tail_damage_rect(
        a.width, a.height, previous_segments, changed_index, gap, measure_segment);
    const auto next_damage = status_tail_damage_rect(
        a.width, a.height, impl_->segments, changed_index, gap, measure_segment);
    queue_redraw(union_rect(previous_damage, next_damage));
}

void StatusBar::set_segment(std::size_t index, std::string text) {
    if (index >= impl_->segments.size()) {
        throw std::out_of_range("StatusBar::set_segment: index out of range");
    }
    if (impl_->segments[index] != text) {
        const auto previous_segments = impl_->segments;
        impl_->segments[index] = std::move(text);
        ensure_accessible().set_description(status_description(impl_->segments, impl_->trailing));
        const auto a = allocation();
        if (a.width <= 0.0F || a.height <= 0.0F) {
            queue_redraw();
            return;
        }
        const float gap = theme_number("segment-gap", 16.0F);
        const auto font = status_font();
        const auto measure_segment = [&](std::string_view segment_text) {
            return measure_text(segment_text, font);
        };
        const auto previous_damage = status_tail_damage_rect(
            a.width, a.height, previous_segments, index, gap, measure_segment);
        const auto next_damage = status_tail_damage_rect(
            a.width, a.height, impl_->segments, index, gap, measure_segment);
        queue_redraw(union_rect(previous_damage, next_damage));
    }
}

void StatusBar::set_segment_widget(std::size_t index, std::shared_ptr<Widget> widget) {
    if (index >= impl_->segments.size()) {
        throw std::out_of_range("StatusBar::set_segment_widget: index out of range");
    }
    auto& slot = impl_->widgets[index];
    if (slot == widget) {
        return;
    }
    if (slot != nullptr) {
        remove_child(*slot);
    }
    slot = std::move(widget);
    if (slot != nullptr) {
        append_child(slot);
    }
    queue_layout();
    queue_redraw();
}

Widget* StatusBar::segment_widget(std::size_t index) const {
    if (index >= impl_->segments.size()) {
        throw std::out_of_range("StatusBar::segment_widget: index out of range");
    }
    return impl_->widgets[index].get();
}

std::size_t StatusBar::segment_count() const {
    return impl_->segments.size();
}

std::string_view StatusBar::segment(std::size_t index) const {
    if (index >= impl_->segments.size()) {
        throw std::out_of_range("StatusBar::segment: index out of range");
    }
    return impl_->segments[index];
}

void StatusBar::set_trailing(std::string text) {
    if (impl_->trailing == text) {
        return;
    }
    const auto previous_trailing = impl_->trailing;
    impl_->trailing = std::move(text);
    ensure_accessible().set_description(status_description(impl_->segments, impl_->trailing));
    const auto a = allocation();
    if (a.width <= 0.0F || a.height <= 0.0F) {
        queue_redraw();
        return;
    }
    const auto font = status_font();
    const auto measure_trailing = [&](std::string_view value) { return measure_text(value, font); };
    queue_redraw(
        union_rect(trailing_damage_rect(a.width, a.height, previous_trailing, measure_trailing),
                   trailing_damage_rect(a.width, a.height, impl_->trailing, measure_trailing)));
}

std::string_view StatusBar::trailing() const {
    return impl_->trailing;
}

bool StatusBar::shows_resize_grip() const {
    const auto* window = host_window();
    return window != nullptr && window->is_resizable() && !window->is_maximized() &&
           window->uses_client_side_decorations();
}

StatusBar::Placement StatusBar::placement() const {
    Placement placed;
    const auto bounds = allocation();
    const float gap = theme_number("segment-gap", 16.0F);
    const auto font = status_font();
    placed.has_grip = shows_resize_grip();
    if (placed.has_grip) {
        placed.grip = {
            bounds.right() - kGripSize, bounds.bottom() - kGripSize, kGripSize, kGripSize};
    }

    float trailing_width = 0.0F;
    if (!impl_->trailing.empty()) {
        trailing_width = measure_text(impl_->trailing, font).width;
    }
    const float right_inset = 16.0F + (placed.has_grip ? kGripSize : 0.0F);
    const float content_right =
        bounds.x + std::max(0.0F, bounds.width - right_inset - trailing_width);
    if (!impl_->trailing.empty()) {
        const float text_x = std::max(bounds.x, content_right);
        placed.trailing = {
            text_x, bounds.y, std::max(0.0F, bounds.right() - right_inset - text_x), bounds.height};
    }

    float x = bounds.x + 16.0F;
    placed.segments.resize(impl_->segments.size());
    for (std::size_t index = 0; index < impl_->segments.size(); ++index) {
        float width = 0.0F;
        if (index < impl_->widgets.size() && impl_->widgets[index] != nullptr) {
            width = impl_->widgets[index]->measure(Constraints::unbounded()).natural_width;
        } else {
            width = measure_text(impl_->segments[index], font).width;
        }
        width = std::min(width, std::max(0.0F, content_right - x));
        const float slot_height = std::max(0.0F, bounds.height - 8.0F);
        placed.segments[index].width = width;
        placed.segments[index].bounds = {x, bounds.y + 4.0F, width, slot_height};
        x += width + gap;
    }
    return placed;
}

void StatusBar::resize_from_grip(Point point) {
    auto* window = host_window();
    if (window == nullptr || !shows_resize_grip()) {
        return;
    }
    const float width = impl_->grip_start_size.width + (point.x - impl_->grip_press.x);
    const float height = impl_->grip_start_size.height + (point.y - impl_->grip_press.y);
    window->resize(std::max(1, static_cast<int>(std::lround(width))),
                   std::max(1, static_cast<int>(std::lround(height))));
}

SizeRequest StatusBar::measure(const Constraints& constraints) const {
    const auto font = status_font();
    float tallest_segment = 0.0F;
    for (std::size_t index = 0; index < impl_->segments.size(); ++index) {
        float height = measure_text(impl_->segments[index], font).height;
        if (index < impl_->widgets.size() && impl_->widgets[index] != nullptr) {
            height = std::max(height, impl_->widgets[index]->measure(constraints).natural_height);
        }
        tallest_segment = std::max(tallest_segment, height);
    }
    if (!impl_->trailing.empty()) {
        tallest_segment = std::max(tallest_segment, measure_text(impl_->trailing, font).height);
    }
    const float content_height = tallest_segment > 0.0F ? tallest_segment + 10.0F : 0.0F;
    const float h = std::max(theme_number("min-height", 28.0F), content_height);
    const float w = constraints.max_width;
    return {0, h, w, h};
}

void StatusBar::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    const auto placed = placement();
    for (std::size_t index = 0; index < placed.segments.size() && index < impl_->widgets.size();
         ++index) {
        if (impl_->widgets[index] != nullptr) {
            impl_->widgets[index]->allocate(placed.segments[index].bounds);
        }
    }
}

bool StatusBar::handle_mouse_event(const MouseEvent& event) {
    const bool grip = shows_resize_grip();
    const Point point{event.x, event.y};
    const bool on_grip = grip && placement().grip.contains(point);
    switch (event.type) {
    case MouseEvent::Type::Press:
        if (event.button != 1 || !on_grip) {
            return false;
        }
        impl_->grip_armed = true;
        impl_->grip_press = point;
        impl_->grip_start_size = host_window()->size();
        return true;
    case MouseEvent::Type::Move:
        if (impl_->grip_armed) {
            resize_from_grip(point);
            return true;
        }
        if (impl_->grip_hovered != on_grip) {
            impl_->grip_hovered = on_grip;
            queue_redraw();
        }
        return on_grip;
    case MouseEvent::Type::Release:
        if (!impl_->grip_armed || event.button != 1) {
            return false;
        }
        impl_->grip_armed = false;
        resize_from_grip(point);
        return true;
    case MouseEvent::Type::Enter:
        return on_grip;
    case MouseEvent::Type::Leave:
        if (impl_->grip_hovered) {
            impl_->grip_hovered = false;
            queue_redraw();
        }
        return false;
    case MouseEvent::Type::Scroll:
    case MouseEvent::Type::DragStart:
    case MouseEvent::Type::DragUpdate:
    case MouseEvent::Type::DragEnd:
        return false;
    }
    return false;
}

CursorShape StatusBar::cursor_shape() const {
    return impl_->grip_hovered || impl_->grip_armed ? CursorShape::ResizeLeftRight
                                                    : CursorShape::Default;
}

void StatusBar::snapshot(SnapshotContext& ctx) const {
    const auto bounds = allocation();
    ctx.add_color_rect(bounds, theme_color("background", Color{0.94F, 0.95F, 0.97F, 1.0F}));
    ctx.add_color_rect({bounds.x, bounds.y, bounds.width, 1}, theme_color("border-color"));

    const float gap = theme_number("segment-gap", 16.0F);
    const auto text_color = theme_color("text-color", Color{0.35F, 0.37F, 0.42F, 1.0F});
    const auto separator_color = theme_color("separator-color");
    const auto font = status_font();
    const auto placed = placement();

    for (std::size_t index = 0; index < impl_->segments.size(); ++index) {
        const bool has_widget = index < impl_->widgets.size() && impl_->widgets[index] != nullptr;
        if (!has_widget) {
            const auto& text = impl_->segments[index];
            const auto measured = measure_text(text, font);
            const auto& slot = placed.segments[index].bounds;
            const float text_y =
                bounds.y + std::max(0.0F, (bounds.height - measured.height) * 0.5F);
            ctx.add_text({slot.x, text_y}, text, text_color, font);
        }
        if (index + 1 < impl_->segments.size()) {
            const float divider_x = placed.segments[index].bounds.right() + (gap * 0.5F);
            ctx.add_color_rect({divider_x, bounds.y + 5.0F, 1.0F, bounds.height - 10.0F},
                               separator_color);
        }
    }

    if (!impl_->trailing.empty()) {
        const auto trailing_color = theme_color("text-secondary", Color::from_rgb(96, 103, 114));
        const auto measured = measure_text(impl_->trailing, font);
        const float text_y = bounds.y + std::max(0.0F, (bounds.height - measured.height) * 0.5F);
        ctx.add_text({placed.trailing.x, text_y}, impl_->trailing, trailing_color, font);
    }

    if (placed.has_grip) {
        const auto grip_color = theme_color("separator-color", Color{0.55F, 0.57F, 0.62F, 1.0F});
        const auto& grip = placed.grip;
        for (int mark = 0; mark < 3; ++mark) {
            const float offset = 3.0F + (static_cast<float>(mark) * 3.0F);
            ctx.add_line({grip.right() - offset, grip.bottom() - 3.0F},
                         {grip.right() - 3.0F, grip.bottom() - offset},
                         grip_color,
                         1.0F);
        }
    }
    Widget::snapshot(ctx);
}

} // namespace nk
