#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/level_bar.h>

namespace nk {

struct LevelBar::Impl {
    int value = 0;
    int maximum = 4;
};

std::shared_ptr<LevelBar> LevelBar::create() {
    return std::shared_ptr<LevelBar>(new LevelBar());
}

LevelBar::LevelBar() : impl_(std::make_unique<Impl>()) {
    add_style_class("level-bar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Meter);
    accessible.set_name("Level");
    publish_value();
}

LevelBar::~LevelBar() = default;

int LevelBar::value() const {
    return impl_->value;
}

void LevelBar::set_value(int value) {
    const int clamped = std::clamp(value, 0, impl_->maximum);
    if (impl_->value == clamped) {
        return;
    }
    impl_->value = clamped;
    publish_value();
    queue_redraw();
}

int LevelBar::maximum() const {
    return impl_->maximum;
}

void LevelBar::set_maximum(int maximum) {
    const int clamped = std::max(1, maximum);
    if (impl_->maximum == clamped) {
        return;
    }
    impl_->maximum = clamped;
    impl_->value = std::min(impl_->value, impl_->maximum);
    publish_value();
    queue_layout();
    queue_redraw();
}

void LevelBar::publish_value() {
    ensure_accessible().set_value(std::to_string(impl_->value) + " of " +
                                  std::to_string(impl_->maximum));
}

SizeRequest LevelBar::measure(const Constraints& /*constraints*/) const {
    const float width = theme_number("width", 96.0F);
    const float height = theme_number("height", 16.0F);
    return {width, height, width, height};
}

void LevelBar::snapshot(SnapshotContext& ctx) const {
    const auto bounds = allocation();
    if (bounds.width <= 0.0F || bounds.height <= 0.0F || impl_->maximum <= 0) {
        return;
    }
    const float gap = theme_number("gap", 4.0F);
    const float total_gap = gap * static_cast<float>(impl_->maximum - 1);
    const float block_width =
        std::max(1.0F, (bounds.width - total_gap) / static_cast<float>(impl_->maximum));
    const float radius = theme_number("corner-radius", 2.0F);
    const auto filled = theme_color("fill-color", Color{0.2F, 0.45F, 0.85F, 1.0F});
    const auto empty = theme_color("track-color", Color{0.88F, 0.89F, 0.91F, 1.0F});
    for (int index = 0; index < impl_->maximum; ++index) {
        const float x = bounds.x + static_cast<float>(index) * (block_width + gap);
        ctx.add_rounded_rect({x, bounds.y, block_width, bounds.height},
                             index < impl_->value ? filled : empty,
                             radius);
    }
}

} // namespace nk
