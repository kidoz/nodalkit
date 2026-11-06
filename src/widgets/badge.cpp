#include <algorithm>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/badge.h>
#include <string>

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
    std::string rendered;
    bool counting = false;
    int count = 0;
    int cap = 99;
    bool dot = false;
    bool conveys_status = false;
};

std::shared_ptr<Badge> Badge::create(std::string text) {
    return std::shared_ptr<Badge>(new Badge(std::move(text)));
}

std::shared_ptr<Badge> Badge::create(int count) {
    auto badge = create(std::string{});
    badge->set_count(count);
    return badge;
}

Badge::Badge(std::string text) : impl_(std::make_unique<Impl>()) {
    impl_->text = std::move(text);
    add_style_class("badge");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Label);
    refresh_accessible();
    accessible.set_hidden(false);
}

Badge::~Badge() = default;

std::string Badge::display_text() const {
    if (!impl_->counting) {
        return impl_->text;
    }
    if (impl_->count > impl_->cap) {
        return std::to_string(impl_->cap) + "+";
    }
    return std::to_string(impl_->count);
}

void Badge::refresh_accessible() {
    impl_->rendered = display_text();
    auto& accessible = ensure_accessible();
    accessible.set_name(impl_->rendered);
    accessible.set_role(impl_->conveys_status ? AccessibleRole::Status : AccessibleRole::Label);
}

std::string_view Badge::text() const {
    return impl_->rendered;
}

void Badge::set_text(std::string text) {
    impl_->counting = false;
    if (impl_->text == text && impl_->rendered == text) {
        return;
    }
    impl_->text = std::move(text);
    refresh_accessible();
    queue_layout();
    queue_redraw();
}

void Badge::set_count(int count) {
    impl_->counting = true;
    impl_->count = std::max(0, count);
    refresh_accessible();
    queue_layout();
    queue_redraw();
}

int Badge::count() const {
    return impl_->count;
}

void Badge::set_count_cap(int cap) {
    impl_->cap = std::max(1, cap);
    if (impl_->counting) {
        refresh_accessible();
        queue_layout();
        queue_redraw();
    }
}

int Badge::count_cap() const {
    return impl_->cap;
}

void Badge::set_dot(bool dot) {
    if (impl_->dot == dot) {
        return;
    }
    impl_->dot = dot;
    queue_layout();
    queue_redraw();
}

bool Badge::is_dot() const {
    return impl_->dot;
}

void Badge::set_conveys_status(bool conveys_status) {
    if (impl_->conveys_status == conveys_status) {
        return;
    }
    impl_->conveys_status = conveys_status;
    refresh_accessible();
}

bool Badge::conveys_status() const {
    return impl_->conveys_status;
}

SizeRequest Badge::measure(const Constraints& /*constraints*/) const {
    const float height = theme_number("height", 20.0F);
    if (impl_->dot) {
        const float dot = theme_number("dot-size", 8.0F);
        return {dot, dot, dot, dot};
    }
    const auto font = badge_font();
    const auto measured = measure_text(impl_->rendered, font);
    const float padding_x = theme_number("padding-x", 12.0F);
    const float min_width = theme_number("min-width", 20.0F);
    const float width = std::max(min_width, measured.width + padding_x * 2.0F);
    return {min_width, height, width, height};
}

void Badge::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float pill_radius = a.height * 0.5F;

    // Pill background.
    ctx.add_rounded_rect(a, theme_color("background", Color{0.3F, 0.56F, 0.9F, 1.0F}), pill_radius);

    if (impl_->dot) {
        return;
    }

    const auto font = badge_font();
    const auto measured = measure_text(impl_->rendered, font);
    const float text_x = a.x + std::max(0.0F, (a.width - measured.width) * 0.5F);
    const float text_y = a.y + std::max(0.0F, (a.height - measured.height) * 0.5F);
    ctx.add_text({text_x, text_y},
                 impl_->rendered,
                 theme_color("text-color", Color{1.0F, 1.0F, 1.0F, 1.0F}),
                 font);
}

} // namespace nk
