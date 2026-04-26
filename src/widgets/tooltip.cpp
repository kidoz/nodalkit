#include <algorithm>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/tooltip.h>

namespace nk {

namespace {

FontDescriptor tooltip_font() {
    return FontDescriptor{
        .family = {},
        .size = 12.0F,
        .weight = FontWeight::Regular,
    };
}

} // namespace

struct Tooltip::Impl {
    std::string text;
};

std::shared_ptr<Tooltip> Tooltip::create(std::string text) {
    return std::shared_ptr<Tooltip>(new Tooltip(std::move(text)));
}

Tooltip::Tooltip(std::string text) : impl_(std::make_unique<Impl>()) {
    impl_->text = std::move(text);
    add_style_class("tooltip");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Label);
    accessible.set_hidden(true);
}

Tooltip::~Tooltip() = default;

std::string_view Tooltip::text() const {
    return impl_->text;
}

void Tooltip::set_text(std::string text) {
    if (impl_->text != text) {
        impl_->text = std::move(text);
        queue_layout();
        queue_redraw();
    }
}

SizeRequest Tooltip::measure(const Constraints& /*constraints*/) const {
    const float padding_x = theme_number("padding-x", 8.0F);
    const float padding_y = theme_number("padding-y", 4.0F);
    const auto measured = measure_text(impl_->text, tooltip_font());
    const float w = measured.width + (padding_x * 2.0F);
    const float h = measured.height + (padding_y * 2.0F);
    return {w, h, w, h};
}

void Tooltip::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float padding_x = theme_number("padding-x", 8.0F);
    const float corner_radius = theme_number("corner-radius", 4.0F);

    // Dark background.
    ctx.add_rounded_rect(
        a, theme_color("background", Color{0.15F, 0.15F, 0.17F, 0.95F}), corner_radius);

    // White text.
    const auto font = tooltip_font();
    const auto measured = measure_text(impl_->text, font);
    const float text_x = a.x + padding_x;
    const float text_y = a.y + std::max(0.0F, (a.height - measured.height) * 0.5F);
    ctx.add_text({text_x, text_y},
                 std::string(impl_->text),
                 theme_color("text-color", Color{1.0F, 1.0F, 1.0F, 1.0F}),
                 font);
}

} // namespace nk
