#include "indeterminate_motion.h"

#include <algorithm>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/progress_bar.h>

namespace nk {

struct ProgressBar::Impl {
    explicit Impl(ProgressBar& owner) : motion(owner, "progress-motion") {}

    float fraction = 0.0F;
    detail::IndeterminateMotion motion;
    ScopedConnection map_connection;
    ScopedConnection unmap_connection;
};

std::shared_ptr<ProgressBar> ProgressBar::create() {
    return std::shared_ptr<ProgressBar>(new ProgressBar());
}

ProgressBar::ProgressBar() : impl_(std::make_unique<Impl>(*this)) {
    add_style_class("progress-bar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::ProgressBar);
    accessible.set_value("0%");
    impl_->motion.set_host_gate([this] {
        const Window* host = host_window();
        return host != nullptr && host->is_visible();
    });
    impl_->map_connection = ScopedConnection(on_map().connect([this] { sync_motion(); }));
    impl_->unmap_connection = ScopedConnection(on_unmap().connect([this] { sync_motion(); }));
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
        impl_->motion.set_active(fraction < 0.0F);
        queue_redraw();
    }
}

void ProgressBar::pulse() {
    impl_->motion.nudge(0.1F);
}

SizeRequest ProgressBar::measure(const Constraints& /*constraints*/) const {
    const float w = theme_number("width", 200.0F);
    const float h = theme_number("height", 6.0F);
    return {w, h, w, h};
}

void ProgressBar::on_host_window_changed(Window* /*window*/) {
    sync_motion();
}

void ProgressBar::sync_motion() {
    impl_->motion.sync();
}

void ProgressBar::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 3.0F);

    ctx.add_rounded_rect(
        a, theme_color("track-color", Color{0.88F, 0.89F, 0.91F, 1.0F}), corner_radius);

    const auto fill_color = theme_color("fill-color", Color{0.2F, 0.45F, 0.85F, 1.0F});

    if (impl_->fraction < 0.0F) {
        const float segment_width = a.width * 0.3F;
        const float offset_x = impl_->motion.phase() * (a.width + segment_width) - segment_width;
        const float clip_x = std::max(a.x, a.x + offset_x);
        const float clip_right = std::min(a.right(), a.x + offset_x + segment_width);
        const float clip_w = std::max(0.0F, clip_right - clip_x);
        if (clip_w > 0.0F) {
            ctx.add_rounded_rect({clip_x, a.y, clip_w, a.height}, fill_color, corner_radius);
        }
    } else {
        const float fill_w = a.width * std::clamp(impl_->fraction, 0.0F, 1.0F);
        if (fill_w > 0.0F) {
            ctx.add_rounded_rect({a.x, a.y, fill_w, a.height}, fill_color, corner_radius);
        }
    }
}

} // namespace nk
