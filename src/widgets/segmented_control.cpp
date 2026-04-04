#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/segmented_control.h>
#include <stdexcept>

namespace nk {

namespace {

FontDescriptor segmented_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

std::string join_segments(const std::vector<std::string>& segments) {
    std::string result;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (index > 0) {
            result += ", ";
        }
        result += segments[index];
    }
    return result;
}

} // namespace

struct SegmentedControl::Impl {
    std::vector<std::string> segments;
    Signal<int> selection_changed;
    int selected_index = -1;
    int hovered_index = -1;
    int armed_index = -1;
};

std::shared_ptr<SegmentedControl> SegmentedControl::create() {
    return std::shared_ptr<SegmentedControl>(new SegmentedControl());
}

SegmentedControl::SegmentedControl() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("segmented-control");

    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::TabList);
    accessible.set_description({});
    accessible.set_value({});
}

SegmentedControl::~SegmentedControl() = default;

void SegmentedControl::set_segments(std::vector<std::string> segments) {
    impl_->segments = std::move(segments);
    if (impl_->segments.empty()) {
        impl_->selected_index = -1;
        impl_->hovered_index = -1;
        impl_->armed_index = -1;
    } else if (impl_->selected_index < 0 ||
               impl_->selected_index >= static_cast<int>(impl_->segments.size())) {
        impl_->selected_index = 0;
    }

    auto& accessible = ensure_accessible();
    accessible.set_description(join_segments(impl_->segments));
    accessible.set_value(std::string(selected_text()));

    queue_layout();
    queue_redraw();
}

std::size_t SegmentedControl::segment_count() const {
    return impl_->segments.size();
}

std::string_view SegmentedControl::segment(std::size_t index) const {
    if (index >= impl_->segments.size()) {
        throw std::out_of_range("SegmentedControl::segment: index out of range");
    }
    return impl_->segments[index];
}

int SegmentedControl::selected_index() const {
    return impl_->selected_index;
}

void SegmentedControl::set_selected_index(int index) {
    if (index < 0 || index >= static_cast<int>(impl_->segments.size())) {
        throw std::out_of_range("SegmentedControl::set_selected_index: index out of range");
    }
    if (impl_->selected_index == index) {
        return;
    }

    impl_->selected_index = index;
    ensure_accessible().set_value(std::string(selected_text()));
    queue_redraw();
    impl_->selection_changed.emit(index);
}

std::string_view SegmentedControl::selected_text() const {
    if (impl_->selected_index < 0 ||
        impl_->selected_index >= static_cast<int>(impl_->segments.size())) {
        return {};
    }
    return impl_->segments[static_cast<std::size_t>(impl_->selected_index)];
}

Signal<int>& SegmentedControl::on_selection_changed() {
    return impl_->selection_changed;
}

SizeRequest SegmentedControl::measure(const Constraints& /*constraints*/) const {
    const auto font = segmented_font();
    const float min_height = theme_number("min-height", 36.0F);
    const float min_segment_width = theme_number("min-segment-width", 84.0F);
    const float padding_x = theme_number("padding-x", 16.0F);
    const float track_padding = theme_number("track-padding", 4.0F);

    float width = track_padding * 2.0F;
    for (const auto& label : impl_->segments) {
        const auto measured = measure_text(label, font);
        width += std::max(min_segment_width, measured.width + (padding_x * 2.0F));
    }

    if (impl_->segments.empty()) {
        width += min_segment_width;
    }

    return {width, min_height, width, min_height};
}

bool SegmentedControl::handle_mouse_event(const MouseEvent& event) {
    if (impl_->segments.empty()) {
        return false;
    }

    if (event.button != 1 && event.type != MouseEvent::Type::Move &&
        event.type != MouseEvent::Type::Enter && event.type != MouseEvent::Type::Leave) {
        return false;
    }

    const auto body = allocation();
    const float count = static_cast<float>(impl_->segments.size());
    const float segment_width = count > 0.0F ? body.width / count : 0.0F;

    auto index_at = [&](Point point) -> int {
        if (!body.contains(point) || segment_width <= 0.0F) {
            return -1;
        }
        const auto raw = static_cast<int>((point.x - body.x) / segment_width);
        return std::clamp(raw, 0, static_cast<int>(impl_->segments.size()) - 1);
    };

    const auto point = Point{event.x, event.y};
    const int index = index_at(point);

    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->armed_index = index;
        impl_->hovered_index = index;
        if (index >= 0) {
            grab_focus();
            queue_redraw();
            return true;
        }
        return false;
    case MouseEvent::Type::Release: {
        const bool activated = impl_->armed_index >= 0 && impl_->armed_index == index;
        const int armed_index = impl_->armed_index;
        impl_->armed_index = -1;
        impl_->hovered_index = index;
        queue_redraw();
        if (activated) {
            set_selected_index(armed_index);
            return true;
        }
        return false;
    }
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
        if (impl_->hovered_index != index) {
            impl_->hovered_index = index;
            queue_redraw();
        }
        return index >= 0;
    case MouseEvent::Type::Leave:
        if (impl_->hovered_index != -1) {
            impl_->hovered_index = -1;
            queue_redraw();
        }
        return false;
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool SegmentedControl::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press || impl_->segments.empty()) {
        return false;
    }

    const auto last_index = static_cast<int>(impl_->segments.size()) - 1;
    const int current = impl_->selected_index >= 0 ? impl_->selected_index : 0;

    switch (event.key) {
    case KeyCode::Left:
    case KeyCode::Up:
        set_selected_index(current > 0 ? current - 1 : 0);
        return true;
    case KeyCode::Right:
    case KeyCode::Down:
        set_selected_index(current < last_index ? current + 1 : last_index);
        return true;
    case KeyCode::Home:
        set_selected_index(0);
        return true;
    case KeyCode::End:
        set_selected_index(last_index);
        return true;
    case KeyCode::Return:
    case KeyCode::Space:
        if (impl_->selected_index < 0) {
            set_selected_index(0);
        }
        return true;
    default:
        return false;
    }
}

CursorShape SegmentedControl::cursor_shape() const {
    return CursorShape::PointingHand;
}

void SegmentedControl::on_focus_changed(bool focused) {
    if (!focused && (impl_->hovered_index != -1 || impl_->armed_index != -1)) {
        impl_->hovered_index = -1;
        impl_->armed_index = -1;
        queue_redraw();
    }
}

void SegmentedControl::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 12.0F);
    const float selection_radius = theme_number("selection-radius", 10.0F);
    const float track_padding = theme_number("track-padding", 4.0F);
    const float separator_inset = theme_number("separator-inset", 8.0F);
    auto body = a;

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a,
                             theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
                             corner_radius + 2.0F);
        body = {a.x + 2.0F, a.y + 2.0F, a.width - 4.0F, a.height - 4.0F};
    }

    ctx.add_rounded_rect(
        body, theme_color("background", Color{0.95F, 0.96F, 0.98F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F}), 1.0F, corner_radius);

    if (impl_->segments.empty()) {
        return;
    }

    const float segment_width = body.width / static_cast<float>(impl_->segments.size());
    const auto font = segmented_font();

    for (std::size_t index = 0; index < impl_->segments.size(); ++index) {
        const auto segment_rect = Rect{
            body.x + segment_width * static_cast<float>(index), body.y, segment_width, body.height};
        const bool selected = static_cast<int>(index) == impl_->selected_index;
        const bool hovered = static_cast<int>(index) == impl_->hovered_index;
        const bool armed = static_cast<int>(index) == impl_->armed_index;

        auto fill_rect = Rect{
            segment_rect.x + track_padding,
            segment_rect.y + track_padding,
            std::max(0.0F, segment_rect.width - track_padding * 2.0F),
            std::max(0.0F, segment_rect.height - track_padding * 2.0F),
        };

        if (selected) {
            ctx.add_rounded_rect(fill_rect,
                                 theme_color("selected-background", Color{1.0F, 1.0F, 1.0F, 1.0F}),
                                 selection_radius);
            ctx.add_border(
                fill_rect,
                theme_color("selected-border-color",
                            theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F})),
                1.0F,
                selection_radius);
        } else if (armed) {
            ctx.add_rounded_rect(fill_rect,
                                 theme_color("pressed-background", Color{0.9F, 0.92F, 0.95F, 1.0F}),
                                 selection_radius);
        } else if (hovered) {
            ctx.add_rounded_rect(fill_rect,
                                 theme_color("hover-background", Color{0.97F, 0.98F, 0.99F, 1.0F}),
                                 selection_radius);
        }

        if (index + 1 < impl_->segments.size() &&
            impl_->selected_index != static_cast<int>(index) &&
            impl_->selected_index != static_cast<int>(index + 1)) {
            const float separator_x = segment_rect.right();
            ctx.add_color_rect({separator_x,
                                body.y + separator_inset,
                                1.0F,
                                std::max(0.0F, body.height - separator_inset * 2.0F)},
                               theme_color("separator-color", Color{0.84F, 0.86F, 0.9F, 1.0F}));
        }

        const auto measured = measure_text(impl_->segments[index], font);
        const float text_x =
            segment_rect.x + std::max(0.0F, (segment_rect.width - measured.width) * 0.5F);
        const float text_y =
            segment_rect.y + std::max(0.0F, (segment_rect.height - measured.height) * 0.5F);
        ctx.add_text({text_x, text_y},
                     impl_->segments[index],
                     selected ? theme_color("selected-text-color", Color{0.1F, 0.1F, 0.12F, 1.0F})
                              : theme_color("text-color", Color{0.38F, 0.4F, 0.45F, 1.0F}),
                     font);
    }
}

} // namespace nk
