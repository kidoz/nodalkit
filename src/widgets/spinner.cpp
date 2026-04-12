#include <algorithm>
#include <cmath>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/spinner.h>

namespace nk {

namespace {

constexpr int kNumDots = 8;
constexpr float kTwoPi = 2.0F * 3.14159265358979323846F;
constexpr float kStepAngle = kTwoPi / static_cast<float>(kNumDots);

} // namespace

struct Spinner::Impl {
    bool spinning = true;
    float diameter = 24.0F;
    mutable float angle = 0.0F;
};

std::shared_ptr<Spinner> Spinner::create() {
    return std::shared_ptr<Spinner>(new Spinner());
}

Spinner::Spinner() : impl_(std::make_unique<Impl>()) {
    add_style_class("spinner");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::ProgressBar);
    accessible.set_name("Loading");
    accessible.set_hidden(false);
    accessible.set_value("indeterminate");
}

Spinner::~Spinner() = default;

bool Spinner::is_spinning() const {
    return impl_->spinning;
}

void Spinner::set_spinning(bool spinning) {
    if (impl_->spinning != spinning) {
        impl_->spinning = spinning;
        if (spinning) {
            queue_redraw();
        }
    }
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

void Spinner::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float d = std::min(a.width, a.height);

    // Center within allocation.
    const float center_x = a.x + a.width * 0.5F;
    const float center_y = a.y + a.height * 0.5F;

    // Ring and dot geometry.
    const float ring_radius = d * 0.35F;
    const float dot_radius = d * 0.08F;
    const float dot_diameter = dot_radius * 2.0F;

    const Color base_color = theme_color("color", Color{0.4F, 0.45F, 0.5F, 1.0F});

    // Determine which dot index is "active" based on current angle.
    const int active_index =
        static_cast<int>(impl_->angle / kStepAngle) % kNumDots;

    for (int i = 0; i < kNumDots; ++i) {
        const float theta = static_cast<float>(i) * kStepAngle;
        const float dx = center_x + ring_radius * std::cos(theta) - dot_radius;
        const float dy = center_y + ring_radius * std::sin(theta) - dot_radius;

        // Compute opacity: the active dot is brightest, then fading away.
        const int distance = ((i - active_index) % kNumDots + kNumDots) % kNumDots;
        const float opacity = 1.0F - static_cast<float>(distance) / static_cast<float>(kNumDots);
        const float alpha = std::max(0.15F, opacity) * base_color.a;

        const Color dot_color{base_color.r, base_color.g, base_color.b, alpha};
        ctx.add_rounded_rect(Rect{dx, dy, dot_diameter, dot_diameter}, dot_color, dot_radius);
    }

    // Advance angle for next frame when spinning.
    if (impl_->spinning) {
        impl_->angle += kStepAngle;
        if (impl_->angle >= kTwoPi) {
            impl_->angle -= kTwoPi;
        }
        // Caller must drive animation externally (e.g. via a timer
        // calling queue_redraw) since snapshot is const.
    }
}

} // namespace nk
