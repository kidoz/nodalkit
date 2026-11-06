#include "indeterminate_motion.h"

#include <algorithm>
#include <cmath>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/spinner.h>

namespace nk {

namespace {

constexpr int kNumDots = 8;
constexpr float kTwoPi = 2.0F * 3.14159265358979323846F;

} // namespace

struct Spinner::Impl {
    explicit Impl(Spinner& owner) : motion(owner, "spinner-motion") {}

    bool spinning = true;
    float diameter = 24.0F;
    detail::IndeterminateMotion motion;
    ScopedConnection map_connection;
    ScopedConnection unmap_connection;
};

std::shared_ptr<Spinner> Spinner::create() {
    return std::shared_ptr<Spinner>(new Spinner());
}

Spinner::Spinner() : impl_(std::make_unique<Impl>(*this)) {
    add_style_class("spinner");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::ProgressBar);
    accessible.set_name("Loading");
    accessible.set_hidden(false);
    accessible.set_value("indeterminate");
    impl_->motion.set_host_gate([this] {
        const Window* host = host_window();
        return host != nullptr && host->is_visible();
    });
    impl_->motion.set_active(true);
    impl_->map_connection = ScopedConnection(on_map().connect([this] { sync_motion(); }));
    impl_->unmap_connection = ScopedConnection(on_unmap().connect([this] { sync_motion(); }));
}

Spinner::~Spinner() = default;

bool Spinner::is_spinning() const {
    return impl_->spinning;
}

void Spinner::set_spinning(bool spinning) {
    if (impl_->spinning == spinning) {
        return;
    }
    impl_->spinning = spinning;
    impl_->motion.set_active(spinning);
    queue_redraw();
}

float Spinner::diameter() const {
    return impl_->diameter;
}

void Spinner::set_diameter(float diameter) {
    if (impl_->diameter != diameter) {
        impl_->diameter = diameter;
        queue_layout();
        queue_redraw();
    }
}

SizeRequest Spinner::measure(const Constraints& /*constraints*/) const {
    const float d = impl_->diameter;
    return {d, d, d, d};
}

void Spinner::on_host_window_changed(Window* /*window*/) {
    sync_motion();
}

void Spinner::sync_motion() {
    impl_->motion.sync();
}

void Spinner::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float d = std::min(a.width, a.height);

    const float center_x = a.x + a.width * 0.5F;
    const float center_y = a.y + a.height * 0.5F;
    const float ring_radius = d * 0.35F;
    const float dot_radius = d * 0.08F;
    const float dot_diameter = dot_radius * 2.0F;
    const Color base_color = theme_color("color", Color{0.4F, 0.45F, 0.5F, 1.0F});
    const float active = impl_->motion.phase() * static_cast<float>(kNumDots);

    for (int i = 0; i < kNumDots; ++i) {
        const float theta = static_cast<float>(i) * (kTwoPi / static_cast<float>(kNumDots));
        const float dx = center_x + ring_radius * std::cos(theta) - dot_radius;
        const float dy = center_y + ring_radius * std::sin(theta) - dot_radius;

        const float raw = std::fabs(static_cast<float>(i) - active);
        const float distance = std::min(raw, static_cast<float>(kNumDots) - raw);
        const float opacity =
            std::clamp(1.0F - (distance / (static_cast<float>(kNumDots) * 0.5F)), 0.15F, 1.0F);
        const Color dot_color{base_color.r, base_color.g, base_color.b, opacity * base_color.a};
        ctx.add_rounded_rect(Rect{dx, dy, dot_diameter, dot_diameter}, dot_color, dot_radius);
    }
}

} // namespace nk
