#include <algorithm>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/button_content.h>
#include <utility>

namespace nk {

namespace {

constexpr float kIconSize = 16.0F;
constexpr float kGap = 6.0F;
constexpr float kIndicatorSlot = 16.0F;

FontDescriptor content_font() {
    return FontDescriptor{.family = {}, .size = 13.5F, .weight = FontWeight::Medium};
}

bool has_body(std::string_view label, SymbolicIcon icon) {
    return icon != SymbolicIcon::None || !label.empty();
}

void add_icon(SnapshotContext& ctx, SymbolicIcon icon, Rect bounds, Color color) {
    const Point center{bounds.x + (bounds.width * 0.5F), bounds.y + (bounds.height * 0.5F)};
    switch (icon) {
    case SymbolicIcon::None:
        break;
    case SymbolicIcon::Search:
        ctx.add_border({center.x - 5.0F, center.y - 6.0F, 8.0F, 8.0F}, color, 1.5F, 4.0F);
        ctx.add_line(
            {center.x + 1.5F, center.y + 1.0F}, {center.x + 5.0F, center.y + 4.5F}, color, 1.5F);
        break;
    case SymbolicIcon::Add:
        ctx.add_line({center.x - 5.0F, center.y}, {center.x + 5.0F, center.y}, color, 1.5F);
        ctx.add_line({center.x, center.y - 5.0F}, {center.x, center.y + 5.0F}, color, 1.5F);
        break;
    case SymbolicIcon::Close:
        ctx.add_line(
            {center.x - 4.0F, center.y - 4.0F}, {center.x + 4.0F, center.y + 4.0F}, color, 1.5F);
        ctx.add_line(
            {center.x + 4.0F, center.y - 4.0F}, {center.x - 4.0F, center.y + 4.0F}, color, 1.5F);
        break;
    }
}

void add_chevron(SnapshotContext& ctx, Point tip, Color color) {
    ctx.add_line({tip.x - 4.0F, tip.y - 2.0F}, {tip.x, tip.y + 2.0F}, color, 1.5F);
    ctx.add_line({tip.x, tip.y + 2.0F}, {tip.x + 4.0F, tip.y - 2.0F}, color, 1.5F);
}

} // namespace

struct ButtonContent::Impl {
    std::string label;
    SymbolicIcon icon = SymbolicIcon::None;
    bool shows_indicator = false;
};

std::shared_ptr<ButtonContent> ButtonContent::create(std::string label, SymbolicIcon icon) {
    return std::shared_ptr<ButtonContent>(new ButtonContent(std::move(label), icon));
}

ButtonContent::ButtonContent(std::string label, SymbolicIcon icon)
    : impl_(std::make_unique<Impl>()) {
    impl_->label = std::move(label);
    impl_->icon = icon;
    add_style_class("button-content");
    ensure_accessible().set_hidden(true);
}

ButtonContent::~ButtonContent() = default;

std::string_view ButtonContent::label() const {
    return impl_->label;
}

void ButtonContent::set_label(std::string label) {
    if (impl_->label == label) {
        return;
    }
    impl_->label = std::move(label);
    queue_layout();
    queue_redraw();
}

SymbolicIcon ButtonContent::icon() const {
    return impl_->icon;
}

void ButtonContent::set_icon(SymbolicIcon icon) {
    if (impl_->icon == icon) {
        return;
    }
    impl_->icon = icon;
    queue_layout();
    queue_redraw();
}

bool ButtonContent::shows_indicator() const {
    return impl_->shows_indicator;
}

void ButtonContent::set_shows_indicator(bool show) {
    if (impl_->shows_indicator == show) {
        return;
    }
    impl_->shows_indicator = show;
    queue_layout();
    queue_redraw();
}

SizeRequest ButtonContent::measure(const Constraints& /*constraints*/) const {
    if (!has_body(impl_->label, impl_->icon)) {
        return {};
    }
    float width = 0.0F;
    float height = 0.0F;
    if (impl_->icon != SymbolicIcon::None) {
        width += kIconSize;
        height = kIconSize;
    }
    if (!impl_->label.empty()) {
        if (width > 0.0F) {
            width += kGap;
        }
        const auto measured = measure_text(impl_->label, content_font());
        width += measured.width;
        height = std::max(height, measured.height);
    }
    if (impl_->shows_indicator) {
        if (width > 0.0F) {
            width += kGap;
        }
        width += kIndicatorSlot;
        height = std::max(height, kIndicatorSlot);
    }
    return {width, height, width, height};
}

void ButtonContent::snapshot(SnapshotContext& ctx) const {
    if (!has_body(impl_->label, impl_->icon)) {
        return;
    }
    const auto bounds = allocation();
    const auto color = theme_color("text-color");
    const float mid_y = bounds.y + (bounds.height * 0.5F);
    float x = bounds.x;

    if (impl_->icon != SymbolicIcon::None) {
        const Rect icon_bounds{x, mid_y - (kIconSize * 0.5F), kIconSize, kIconSize};
        add_icon(ctx, impl_->icon, icon_bounds, color);
        x += kIconSize;
        if (!impl_->label.empty()) {
            x += kGap;
        }
    }

    const float indicator_width = impl_->shows_indicator ? kIndicatorSlot : 0.0F;
    if (!impl_->label.empty()) {
        const auto font = content_font();
        const auto measured = measure_text(impl_->label, font);
        const float text_y = mid_y - (measured.height * 0.5F);
        const float max_width = std::max(
            0.0F, bounds.right() - x - indicator_width - (indicator_width > 0.0F ? kGap : 0.0F));
        add_text_elided(ctx, {x, text_y}, impl_->label, measured, max_width, color, font);
    }

    if (impl_->shows_indicator) {
        const float tip_x = bounds.right() - (kIndicatorSlot * 0.5F);
        add_chevron(ctx, {tip_x, mid_y}, color);
    }
}

} // namespace nk
