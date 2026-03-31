#include <nk/widgets/status_bar.h>

#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>

#include <algorithm>
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

} // namespace

struct StatusBar::Impl {
    std::vector<std::string> segments;
};

std::shared_ptr<StatusBar> StatusBar::create() {
    return std::shared_ptr<StatusBar>(new StatusBar());
}

StatusBar::StatusBar()
    : impl_(std::make_unique<Impl>()) {
    add_style_class("status-bar");
}

StatusBar::~StatusBar() = default;

void StatusBar::set_segments(std::vector<std::string> segments) {
    impl_->segments = std::move(segments);
    queue_layout();
    queue_redraw();
}

void StatusBar::set_segment(std::size_t index, std::string text) {
    if (index >= impl_->segments.size()) {
        throw std::out_of_range("StatusBar::set_segment: index out of range");
    }
    if (impl_->segments[index] != text) {
        impl_->segments[index] = std::move(text);
        queue_redraw();
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

SizeRequest StatusBar::measure(Constraints const& constraints) const {
    float const h = theme_number("min-height", 28.0F);
    float const w = constraints.max_width;
    return {0, h, w, h};
}

void StatusBar::snapshot(SnapshotContext& ctx) const {
    auto const a = allocation();
    ctx.add_color_rect(
        a, theme_color("background", Color{0.94F, 0.95F, 0.97F, 1.0F}));
    ctx.add_color_rect({a.x, a.y, a.width, 1},
                       theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F}));

    float const gap = theme_number("segment-gap", 16.0F);
    auto const text_color =
        theme_color("text-color", Color{0.35F, 0.37F, 0.42F, 1.0F});
    auto const separator_color =
        theme_color("separator-color", Color{0.82F, 0.84F, 0.88F, 1.0F});
    auto const font = status_font();

    float x_offset = 16.0F;
    for (std::size_t i = 0; i < impl_->segments.size(); ++i) {
        auto const& seg = impl_->segments[i];
        auto const measured = measure_text(seg, font);
        float const text_y =
            a.y + std::max(0.0F, (a.height - measured.height) * 0.5F);
        ctx.add_text({a.x + x_offset, text_y}, seg, text_color, font);
        x_offset += measured.width + gap;
        if (i + 1 < impl_->segments.size()) {
            ctx.add_color_rect(
                {a.x + x_offset - (gap * 0.5F), a.y + 5.0F, 1.0F, a.height - 10.0F},
                separator_color);
        }
    }
}

} // namespace nk
