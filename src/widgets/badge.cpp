#include <algorithm>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/badge.h>

namespace nk {

namespace {

FontDescriptor badge_font() {
    return FontDescriptor{
        .family = {},
        .size = 11.0F,
        .weight = FontWeight::SemiBold,
    };
}

} // namespace

struct Badge::Impl {
    std::string text;
};

std::shared_ptr<Badge> Badge::create(std::string text) {
    return std::shared_ptr<Badge>(new Badge(std::move(text)));
}

Badge::Badge(std::string text) : impl_(std::make_unique<Impl>()) {
    impl_->text = std::move(text);
    add_style_class("badge");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Label);
    accessible.set_name(impl_->text);
    accessible.set_hidden(false);
}

Badge::~Badge() = default;

std::string_view Badge::text() const {
    return impl_->text;
}

void Badge::set_text(std::string text) {
    if (impl_->text != text) {
        impl_->text = std::move(text);
        ensure_accessible().set_name(impl_->text);
        queue_layout();
        queue_redraw();
    }
}

SizeRequest Badge::measure(const Constraints& /*constraints*/) const {
    const auto font = badge_font();
    const auto measured = measure_text(impl_->text, font);
    const float padding_x = theme_number("padding-x", 12.0F);
    const float height = theme_number("height", 20.0F);
    const float min_width = theme_number("min-width", 20.0F);
    const float w = std::max(min_width, measured.width + padding_x * 2.0F);
    return {min_width, height, w, height};
}

void Badge::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float pill_radius = a.height * 0.5F;

    // Pill background.
    ctx.add_rounded_rect(a, theme_color("background", Color{0.3F, 0.56F, 0.9F, 1.0F}), pill_radius);

    // Text centered.
    const auto font = badge_font();
    const auto measured = measure_text(impl_->text, font);
    const float text_x = a.x + std::max(0.0F, (a.width - measured.width) * 0.5F);
    const float text_y = a.y + std::max(0.0F, (a.height - measured.height) * 0.5F);
    ctx.add_text({text_x, text_y},
                 std::string(impl_->text),
                 theme_color("text-color", Color{1.0F, 1.0F, 1.0F, 1.0F}),
                 font);
}

} // namespace nk
