#include <algorithm>
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

} // namespace

struct StatusBar::Impl {
    std::vector<std::string> segments;
};

std::shared_ptr<StatusBar> StatusBar::create() {
    return std::shared_ptr<StatusBar>(new StatusBar());
}

StatusBar::StatusBar() : impl_(std::make_unique<Impl>()) {
    add_style_class("status-bar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Label);
    accessible.set_name("status bar");
}

StatusBar::~StatusBar() = default;

void StatusBar::set_segments(std::vector<std::string> segments) {
    const auto previous_segments = impl_->segments;
    impl_->segments = std::move(segments);
    ensure_accessible().set_description(join_segments(impl_->segments));
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
    const auto next_damage =
        status_tail_damage_rect(a.width, a.height, impl_->segments, changed_index, gap, measure_segment);
    queue_redraw(union_rect(previous_damage, next_damage));
}

void StatusBar::set_segment(std::size_t index, std::string text) {
    if (index >= impl_->segments.size()) {
        throw std::out_of_range("StatusBar::set_segment: index out of range");
    }
    if (impl_->segments[index] != text) {
        const auto previous_segments = impl_->segments;
        impl_->segments[index] = std::move(text);
        ensure_accessible().set_description(join_segments(impl_->segments));
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
        const auto previous_damage =
            status_tail_damage_rect(a.width, a.height, previous_segments, index, gap, measure_segment);
        const auto next_damage =
            status_tail_damage_rect(a.width, a.height, impl_->segments, index, gap, measure_segment);
        queue_redraw(union_rect(previous_damage, next_damage));
    }
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

SizeRequest StatusBar::measure(const Constraints& constraints) const {
    const auto font = status_font();
    float tallest_segment = 0.0F;
    for (const auto& seg : impl_->segments) {
        tallest_segment = std::max(tallest_segment, measure_text(seg, font).height);
    }
    const float content_height = tallest_segment > 0.0F ? tallest_segment + 10.0F : 0.0F;
    const float h = std::max(theme_number("min-height", 28.0F), content_height);
    const float w = constraints.max_width;
    return {0, h, w, h};
}

void StatusBar::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    ctx.add_color_rect(a, theme_color("background", Color{0.94F, 0.95F, 0.97F, 1.0F}));
    ctx.add_color_rect({a.x, a.y, a.width, 1},
                       theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F}));

    const float gap = theme_number("segment-gap", 16.0F);
    const auto text_color = theme_color("text-color", Color{0.35F, 0.37F, 0.42F, 1.0F});
    const auto separator_color = theme_color("separator-color", Color{0.82F, 0.84F, 0.88F, 1.0F});
    const auto font = status_font();

    float x_offset = 16.0F;
    for (std::size_t i = 0; i < impl_->segments.size(); ++i) {
        const auto& seg = impl_->segments[i];
        const auto measured = measure_text(seg, font);
        const float text_y = a.y + std::max(0.0F, (a.height - measured.height) * 0.5F);
        ctx.add_text({a.x + x_offset, text_y}, seg, text_color, font);
        x_offset += measured.width + gap;
        if (i + 1 < impl_->segments.size()) {
            ctx.add_color_rect({a.x + x_offset - (gap * 0.5F), a.y + 5.0F, 1.0F, a.height - 10.0F},
                               separator_color);
        }
    }
}

} // namespace nk
