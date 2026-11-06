#include <algorithm>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/window_title.h>
#include <utility>

namespace nk {

namespace {

FontDescriptor title_font(float size) {
    return FontDescriptor{.family = {}, .size = size, .weight = FontWeight::Medium};
}

} // namespace

struct WindowTitle::Impl {
    std::string title;
    std::string subtitle;
};

std::shared_ptr<WindowTitle> WindowTitle::create(std::string title, std::string subtitle) {
    return std::shared_ptr<WindowTitle>(new WindowTitle(std::move(title), std::move(subtitle)));
}

WindowTitle::WindowTitle(std::string title, std::string subtitle)
    : impl_(std::make_unique<Impl>()) {
    impl_->title = std::move(title);
    impl_->subtitle = std::move(subtitle);
    add_style_class("window-title");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Label);
    accessible.set_name(impl_->title);
    accessible.set_description(impl_->subtitle);
}

WindowTitle::~WindowTitle() = default;

std::string_view WindowTitle::title() const {
    return impl_->title;
}

void WindowTitle::set_title(std::string title) {
    if (impl_->title == title) {
        return;
    }
    impl_->title = std::move(title);
    ensure_accessible().set_name(impl_->title);
    queue_layout();
    queue_redraw();
}

std::string_view WindowTitle::subtitle() const {
    return impl_->subtitle;
}

void WindowTitle::set_subtitle(std::string subtitle) {
    if (impl_->subtitle == subtitle) {
        return;
    }
    impl_->subtitle = std::move(subtitle);
    ensure_accessible().set_description(impl_->subtitle);
    queue_layout();
    queue_redraw();
}

SizeRequest WindowTitle::measure(const Constraints& constraints) const {
    const auto title_face = title_font(theme_number("title-font-size", 14.0F));
    const auto title_size = measure_text(impl_->title, title_face);
    float width = title_size.width;
    float height = title_size.height;
    if (!impl_->subtitle.empty()) {
        const auto subtitle_size =
            measure_text(impl_->subtitle, title_font(theme_number("subtitle-font-size", 11.0F)));
        width = std::max(width, subtitle_size.width);
        height += 1.0F + subtitle_size.height;
    }
    const float clamped = std::min(width, constraints.max_width);
    return {clamped, height, clamped, height};
}

void WindowTitle::snapshot(SnapshotContext& ctx) const {
    if (impl_->title.empty() && impl_->subtitle.empty()) {
        return;
    }
    const auto bounds = allocation();
    const auto title_face = title_font(theme_number("title-font-size", 14.0F));
    const auto title_size = measure_text(impl_->title, title_face);
    const auto foreground = theme_color("text-color");
    const auto centered_x = [&](float width) {
        return width <= bounds.width ? bounds.x + ((bounds.width - width) * 0.5F) : bounds.x;
    };

    if (impl_->subtitle.empty()) {
        add_text_elided(
            ctx,
            {centered_x(title_size.width), bounds.y + ((bounds.height - title_size.height) * 0.5F)},
            impl_->title,
            title_size,
            bounds.width,
            foreground,
            title_face);
        return;
    }

    const auto subtitle_face = title_font(theme_number("subtitle-font-size", 11.0F));
    const auto subtitle_size = measure_text(impl_->subtitle, subtitle_face);
    const float gap = 1.0F;
    const float block_height = title_size.height + gap + subtitle_size.height;
    const float top = bounds.y + ((bounds.height - block_height) * 0.5F);
    add_text_elided(ctx,
                    {centered_x(title_size.width), top},
                    impl_->title,
                    title_size,
                    bounds.width,
                    foreground,
                    title_face);
    add_text_elided(ctx,
                    {centered_x(subtitle_size.width), top + title_size.height + gap},
                    impl_->subtitle,
                    subtitle_size,
                    bounds.width,
                    theme_color("subtitle-color", theme_color("secondary-text-color")),
                    subtitle_face);
}

} // namespace nk
