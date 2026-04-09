#include <algorithm>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/label.h>

namespace nk {

namespace {

FontDescriptor label_font(const Label& label) {
    FontDescriptor font;
    font.size = label.has_style_class("heading") ? 18.0F : 13.5F;
    font.weight = label.has_style_class("heading") ? FontWeight::Medium : FontWeight::Regular;
    return font;
}

bool rect_is_empty(Rect rect) {
    return rect.width <= 0.0F || rect.height <= 0.0F;
}

Rect union_rect(Rect lhs, Rect rhs) {
    if (rect_is_empty(lhs)) {
        return rhs;
    }
    if (rect_is_empty(rhs)) {
        return lhs;
    }

    const float x0 = std::min(lhs.x, rhs.x);
    const float y0 = std::min(lhs.y, rhs.y);
    const float x1 = std::max(lhs.right(), rhs.right());
    const float y1 = std::max(lhs.bottom(), rhs.bottom());
    return {x0, y0, std::max(0.0F, x1 - x0), std::max(0.0F, y1 - y0)};
}

} // namespace

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
        const auto previous_text = impl_->text;
        impl_->text = std::move(text);
        ensure_accessible().set_name(impl_->text);
        queue_layout();

        const auto a = allocation();
        if (a.width <= 0.0F || a.height <= 0.0F) {
            queue_redraw();
            return;
        }

        const auto font = label_font(*this);
        auto text_damage_bounds = [&](std::string_view content) {
            const auto measured = measure_text(content, font);

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
            const float damage_width =
                has_style_class("heading") ? std::max(measured.width, 24.0F) : measured.width;
            const float damage_height =
                has_style_class("heading") ? measured.height + 5.0F : measured.height;
            return Rect{text_x - a.x, text_y - a.y, damage_width, damage_height};
        };

        const auto damage = union_rect(text_damage_bounds(previous_text), text_damage_bounds(impl_->text));
        if (rect_is_empty(damage)) {
            queue_redraw();
        } else {
            queue_redraw(damage);
        }
    }
}

HAlign Label::h_align() const {
    return impl_->h_align;
}

void Label::set_h_align(HAlign align) {
    impl_->h_align = align;
}

SizeRequest Label::measure(const Constraints& /*constraints*/) const {
    const auto font = label_font(*this);
    const auto measured = measure_text(impl_->text, font);
    const float w = measured.width;
    const float h = std::max(measured.height + (has_style_class("heading") ? 6.0F : 0.0F),
                             has_style_class("heading") ? 28.0F : 20.0F);
    return {w, h, w, h};
}

void Label::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    const auto font = label_font(*this);
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
