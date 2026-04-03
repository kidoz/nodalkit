#include <algorithm>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/label.h>

namespace nk {

struct Label::Impl {
    std::string text;
    HAlign h_align = HAlign::Start;
};

std::shared_ptr<Label> Label::create(std::string text) {
    // std::make_shared can't access protected ctor, so use raw new.
    return std::shared_ptr<Label>(new Label(std::move(text)));
}

Label::Label(std::string text) : impl_(std::make_unique<Impl>()) {
    impl_->text = std::move(text);
    add_style_class("label");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Label);
    accessible.set_name(impl_->text);
}

Label::~Label() = default;

std::string_view Label::text() const {
    return impl_->text;
}

void Label::set_text(std::string text) {
    if (impl_->text != text) {
        impl_->text = std::move(text);
        ensure_accessible().set_name(impl_->text);
        queue_layout();
        queue_redraw();
    }
}

HAlign Label::h_align() const {
    return impl_->h_align;
}

void Label::set_h_align(HAlign align) {
    impl_->h_align = align;
}

SizeRequest Label::measure(const Constraints& /*constraints*/) const {
    FontDescriptor font;
    font.size = has_style_class("heading") ? 18.0F : 13.5F;
    font.weight = has_style_class("heading") ? FontWeight::Medium : FontWeight::Regular;
    const auto measured = measure_text(impl_->text, font);
    const float w = measured.width;
    const float h = std::max(measured.height + (has_style_class("heading") ? 6.0F : 0.0F),
                             has_style_class("heading") ? 28.0F : 20.0F);
    return {w, h, w, h};
}

void Label::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    FontDescriptor font;
    font.size = has_style_class("heading") ? 18.0F : 13.5F;
    font.weight = has_style_class("heading") ? FontWeight::Medium : FontWeight::Regular;
    const auto measured = measure_text(impl_->text, font);

    float text_x = a.x;
    if (impl_->h_align == HAlign::Center) {
        text_x = a.x + std::max(0.0F, (a.width - measured.width) * 0.5F);
    } else if (impl_->h_align == HAlign::End) {
        text_x = a.right() - measured.width;
    }

    float available_height = a.height;
    if (has_style_class("heading")) {
        available_height -= 6.0F;
    }
    const float text_y = a.y + std::max(0.0F, (available_height - measured.height) * 0.5F);
    ctx.add_text({text_x, text_y}, std::string(impl_->text), text_color, font);

    if (has_style_class("heading")) {
        const float line_y = text_y + measured.height + 3.0F;
        ctx.add_color_rect({text_x, line_y, 24.0F, 2.0F},
                           theme_color("accent-color", Color{0.2F, 0.45F, 0.85F, 1.0F}));
    }
}

} // namespace nk
