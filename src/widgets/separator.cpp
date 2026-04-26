#include <nk/render/snapshot_context.h>
#include <nk/widgets/separator.h>

namespace nk {

struct Separator::Impl {
    Orientation orientation = Orientation::Horizontal;
};

std::shared_ptr<Separator> Separator::create(Orientation orientation) {
    return std::shared_ptr<Separator>(new Separator(orientation));
}

Separator::Separator(Orientation orientation) : impl_(std::make_unique<Impl>()) {
    impl_->orientation = orientation;
    add_style_class("separator");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Separator);
    accessible.set_hidden(true);
}

Separator::~Separator() = default;

Orientation Separator::orientation() const {
    return impl_->orientation;
}

SizeRequest Separator::measure(const Constraints& /*constraints*/) const {
    const float thickness = theme_number("thickness", 1.0F);
    if (impl_->orientation == Orientation::Horizontal) {
        return {0.0F, thickness, 9999.0F, thickness};
    }
    return {thickness, 0.0F, thickness, 9999.0F};
}

void Separator::snapshot(SnapshotContext& ctx) const {
    ctx.add_color_rect(allocation(), theme_color("color", Color{0.85F, 0.86F, 0.88F, 1.0F}));
}

} // namespace nk
