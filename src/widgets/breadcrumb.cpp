#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/breadcrumb.h>
#include <stdexcept>

namespace nk {

namespace {

FontDescriptor breadcrumb_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

FontDescriptor separator_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

} // namespace

struct Breadcrumb::Impl {
    std::vector<std::string> segments;
    Signal<std::size_t> navigate;
    int hovered_index = -1;
};

std::shared_ptr<Breadcrumb> Breadcrumb::create() {
    return std::shared_ptr<Breadcrumb>(new Breadcrumb());
}

Breadcrumb::Breadcrumb() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("breadcrumb");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Toolbar);
    accessible.set_name("Breadcrumb");
}

Breadcrumb::~Breadcrumb() = default;

void Breadcrumb::set_path(std::vector<std::string> segments) {
    impl_->segments = std::move(segments);
    impl_->hovered_index = -1;
    queue_layout();
    queue_redraw();
}

void Breadcrumb::push(std::string segment) {
    impl_->segments.push_back(std::move(segment));
    impl_->hovered_index = -1;
    queue_layout();
    queue_redraw();
}

void Breadcrumb::pop_to(std::size_t depth) {
    if (depth < impl_->segments.size()) {
        impl_->segments.resize(depth);
        impl_->hovered_index = -1;
        queue_layout();
        queue_redraw();
    }
}

std::size_t Breadcrumb::depth() const {
    return impl_->segments.size();
}

std::string_view Breadcrumb::segment(std::size_t index) const {
    if (index >= impl_->segments.size()) {
        throw std::out_of_range("Breadcrumb segment index out of range");
    }
    return impl_->segments[index];
}

Signal<std::size_t>& Breadcrumb::on_navigate() {
    return impl_->navigate;
}

SizeRequest Breadcrumb::measure(const Constraints& /*constraints*/) const {
    const auto font = breadcrumb_font();
    const auto sep_font = separator_font();
    const float padding_x = theme_number("padding-x", 8.0F);
    const float separator_spacing = theme_number("separator-spacing", 4.0F);
    const float min_height = theme_number("min-height", 32.0F);

    const auto sep_size = measure_text("\xe2\x80\xba", sep_font); // "›"

    float total_w = padding_x; // Left padding.
    for (std::size_t i = 0; i < impl_->segments.size(); ++i) {
        const auto seg_size = measure_text(impl_->segments[i], font);
        total_w += seg_size.width;
        if (i + 1 < impl_->segments.size()) {
            total_w += separator_spacing + sep_size.width + separator_spacing;
        }
    }
    total_w += padding_x; // Right padding.

    return {total_w, min_height, total_w, min_height};
}

bool Breadcrumb::handle_mouse_event(const MouseEvent& event) {
    const auto a = allocation();
    const Point point{event.x, event.y};

    if (!a.contains(point)) {
        if (impl_->hovered_index != -1) {
            impl_->hovered_index = -1;
            queue_redraw();
        }
        return false;
    }

    const auto font = breadcrumb_font();
    const auto sep_font = separator_font();
    const float padding_x = theme_number("padding-x", 8.0F);
    const float separator_spacing = theme_number("separator-spacing", 4.0F);
    const auto sep_size = measure_text("\xe2\x80\xba", sep_font);

    // Hit-test which segment the pointer is over.
    auto segment_at = [&]() -> int {
        float x = a.x + padding_x;
        for (std::size_t i = 0; i < impl_->segments.size(); ++i) {
            const auto seg_size = measure_text(impl_->segments[i], font);
            if (point.x >= x && point.x < x + seg_size.width) {
                // Last segment is not clickable.
                if (i + 1 == impl_->segments.size()) {
                    return -1;
                }
                return static_cast<int>(i);
            }
            x += seg_size.width;
            if (i + 1 < impl_->segments.size()) {
                x += separator_spacing + sep_size.width + separator_spacing;
            }
        }
        return -1;
    };

    switch (event.type) {
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter: {
        const int idx = segment_at();
        if (idx != impl_->hovered_index) {
            impl_->hovered_index = idx;
            queue_redraw();
        }
        return true;
    }
    case MouseEvent::Type::Leave:
        if (impl_->hovered_index != -1) {
            impl_->hovered_index = -1;
            queue_redraw();
        }
        return false;
    case MouseEvent::Type::Press:
        return event.button == 1 && a.contains(point);
    case MouseEvent::Type::Release: {
        if (event.button != 1) {
            return false;
        }
        const int idx = segment_at();
        if (idx >= 0) {
            impl_->navigate.emit(static_cast<std::size_t>(idx));
            return true;
        }
        return false;
    }
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

CursorShape Breadcrumb::cursor_shape() const {
    return impl_->hovered_index >= 0 ? CursorShape::PointingHand : CursorShape::Default;
}

void Breadcrumb::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const auto font = breadcrumb_font();
    const auto sep_font = separator_font();
    const float padding_x = theme_number("padding-x", 8.0F);
    const float separator_spacing = theme_number("separator-spacing", 4.0F);
    const float min_height = theme_number("min-height", 32.0F);

    const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    const auto link_color = theme_color("link-color", Color{0.2F, 0.4F, 0.8F, 1.0F});
    const auto hover_color = theme_color("hover-color", Color{0.15F, 0.35F, 0.7F, 1.0F});
    const auto sep_color = theme_color("separator-color", Color{0.5F, 0.5F, 0.5F, 1.0F});

    const std::string separator = "\xe2\x80\xba"; // "›"
    const auto sep_size = measure_text(separator, sep_font);

    float x = a.x + padding_x;

    for (std::size_t i = 0; i < impl_->segments.size(); ++i) {
        const auto& seg = impl_->segments[i];
        const auto seg_size = measure_text(seg, font);
        const float text_y = a.y + std::max(0.0F, (min_height - seg_size.height) * 0.5F);
        const bool is_last = (i + 1 == impl_->segments.size());
        const bool is_hovered = (static_cast<int>(i) == impl_->hovered_index);

        // Choose color: last segment is plain text, others are links.
        Color color = is_last ? text_color : (is_hovered ? hover_color : link_color);
        ctx.add_text({x, text_y}, std::string(seg), color, font);

        // Draw underline on hover for clickable segments.
        if (is_hovered && !is_last) {
            const float underline_y = text_y + seg_size.height;
            ctx.add_color_rect({x, underline_y, seg_size.width, 1.0F}, hover_color);
        }

        x += seg_size.width;

        // Draw separator between segments.
        if (!is_last) {
            const float sep_x = x + separator_spacing;
            const float sep_y = a.y + std::max(0.0F, (min_height - sep_size.height) * 0.5F);
            ctx.add_text({sep_x, sep_y}, separator, sep_color, sep_font);
            x = sep_x + sep_size.width + separator_spacing;
        }
    }
}

} // namespace nk
