#include <algorithm>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/visual_effect_view.h>
#include <optional>

namespace nk {

namespace {

struct MaterialStyle {
    const char* style_class;
    const char* theme_token;
    Color fallback;
};

MaterialStyle style_for(VisualEffectMaterial material) {
    switch (material) {
    case VisualEffectMaterial::Sidebar:
        return {"visual-effect-sidebar", "sidebar-background",
                Color{0.92F, 0.93F, 0.95F, 0.72F}};
    case VisualEffectMaterial::HeaderView:
        return {"visual-effect-header", "header-background",
                Color{0.95F, 0.96F, 0.97F, 0.80F}};
    case VisualEffectMaterial::WindowBackground:
        return {"visual-effect-window", "window-background",
                Color{0.96F, 0.96F, 0.97F, 0.85F}};
    case VisualEffectMaterial::Popover:
        return {"visual-effect-popover", "popover-background",
                Color{0.98F, 0.98F, 0.99F, 0.88F}};
    case VisualEffectMaterial::Menu:
        return {"visual-effect-menu", "menu-background",
                Color{0.97F, 0.97F, 0.98F, 0.90F}};
    case VisualEffectMaterial::Tooltip:
        return {"visual-effect-tooltip", "tooltip-background",
                Color{0.12F, 0.12F, 0.14F, 0.88F}};
    }
    return {"visual-effect", "visual-effect-background", Color{0.95F, 0.95F, 0.96F, 0.80F}};
}

} // namespace

struct VisualEffectView::Impl {
    VisualEffectMaterial material;
    std::shared_ptr<Widget> child;
    float corner_radius = 0.0F;
    std::optional<Color> fallback_tint_override;
};

std::shared_ptr<VisualEffectView> VisualEffectView::create(VisualEffectMaterial material) {
    return std::shared_ptr<VisualEffectView>(new VisualEffectView(material));
}

VisualEffectView::VisualEffectView(VisualEffectMaterial material)
    : impl_(std::make_unique<Impl>()) {
    impl_->material = material;
    add_style_class("visual-effect-view");
    add_style_class(style_for(material).style_class);
}

VisualEffectView::~VisualEffectView() = default;

void VisualEffectView::set_child(std::shared_ptr<Widget> child) {
    if (impl_->child) {
        remove_child(*impl_->child);
    }
    impl_->child = std::move(child);
    if (impl_->child) {
        append_child(impl_->child);
    }
    queue_layout();
    queue_redraw();
}

Widget* VisualEffectView::child() const {
    return impl_->child.get();
}

VisualEffectMaterial VisualEffectView::material() const {
    return impl_->material;
}

void VisualEffectView::set_material(VisualEffectMaterial material) {
    if (impl_->material == material) {
        return;
    }
    remove_style_class(style_for(impl_->material).style_class);
    impl_->material = material;
    add_style_class(style_for(material).style_class);
    queue_redraw();
}

float VisualEffectView::corner_radius() const {
    return impl_->corner_radius;
}

void VisualEffectView::set_corner_radius(float radius) {
    radius = std::max(0.0F, radius);
    if (impl_->corner_radius == radius) {
        return;
    }
    impl_->corner_radius = radius;
    queue_redraw();
}

void VisualEffectView::set_fallback_tint(Color color) {
    impl_->fallback_tint_override = color;
    queue_redraw();
}

void VisualEffectView::clear_fallback_tint() {
    impl_->fallback_tint_override.reset();
    queue_redraw();
}

SizeRequest VisualEffectView::measure(const Constraints& constraints) const {
    if (!impl_->child) {
        return {0.0F, 0.0F, 0.0F, 0.0F};
    }
    return impl_->child->measure_for_diagnostics(constraints);
}

void VisualEffectView::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    if (impl_->child) {
        impl_->child->allocate(content_rect());
    }
}

void VisualEffectView::snapshot(SnapshotContext& ctx) const {
    const auto style = style_for(impl_->material);
    const Color tint =
        impl_->fallback_tint_override.value_or(theme_color(style.theme_token, style.fallback));

    const Rect rect = allocation();
    if (impl_->corner_radius > 0.0F) {
        ctx.add_rounded_rect(rect, tint, impl_->corner_radius);
    } else {
        ctx.add_color_rect(rect, tint);
    }

    if (impl_->child) {
        Widget::snapshot(ctx);
    }
}

} // namespace nk
