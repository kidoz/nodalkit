#include <algorithm>
#include <cmath>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/progress_bar.h>

namespace nk {

struct ProgressBar::Impl {
    float fraction = 0.0F;
    float pulse_offset = 0.0F;
};

std::shared_ptr<ProgressBar> ProgressBar::create() {
    return std::shared_ptr<ProgressBar>(new ProgressBar());
}

ProgressBar::ProgressBar() : impl_(std::make_unique<Impl>()) {
    add_style_class("progress-bar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::ProgressBar);
    accessible.set_value("0%");
}

ProgressBar::~ProgressBar() = default;

float ProgressBar::fraction() const {
    return impl_->fraction;
}

void ProgressBar::set_fraction(float fraction) {
    if (impl_->fraction != fraction) {
        impl_->fraction = fraction;
        if (fraction < 0.0F) {
            ensure_accessible().set_value("indeterminate");
        } else {
            const int pct = static_cast<int>(std::clamp(fraction, 0.0F, 1.0F) * 100.0F);
            ensure_accessible().set_value(std::to_string(pct) + "%");
        }
        queue_redraw();
    }
}

void ProgressBar::pulse() {
    impl_->pulse_offset += 0.1F;
    if (impl_->pulse_offset > 1.0F) {
        impl_->pulse_offset -= 1.0F;
    }
    queue_redraw();
}

SizeRequest ProgressBar::measure(const Constraints& /*constraints*/) const {
    const float w = theme_number("width", 200.0F);
    const float h = theme_number("height", 6.0F);
    return {w, h, w, h};
}

void ProgressBar::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 3.0F);

    // Track (full-width background).
    ctx.add_rounded_rect(a,
                         theme_color("track-color", Color{0.88F, 0.89F, 0.91F, 1.0F}),
                         corner_radius);

    const auto fill_color = theme_color("fill-color", Color{0.2F, 0.45F, 0.85F, 1.0F});

    if (impl_->fraction < 0.0F) {
        // Indeterminate: draw a segment at the pulse_offset position.
        const float segment_width = a.width * 0.3F;
        const float offset_x = impl_->pulse_offset * (a.width + segment_width) - segment_width;
        const float clip_x = std::max(a.x, a.x + offset_x);
        const float clip_right = std::min(a.right(), a.x + offset_x + segment_width);
        const float clip_w = std::max(0.0F, clip_right - clip_x);
        if (clip_w > 0.0F) {
            ctx.add_rounded_rect({clip_x, a.y, clip_w, a.height}, fill_color, corner_radius);
        }
    } else {
        // Determinate: fill fraction of the width.
        const float fill_w = a.width * std::clamp(impl_->fraction, 0.0F, 1.0F);
        if (fill_w > 0.0F) {
            ctx.add_rounded_rect({a.x, a.y, fill_w, a.height}, fill_color, corner_radius);
        }
    }
}

} // namespace nk
