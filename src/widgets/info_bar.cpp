#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/info_bar.h>

namespace nk {

namespace {

FontDescriptor info_bar_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

Color background_for_severity(InfoBarSeverity severity) {
    switch (severity) {
    case InfoBarSeverity::Info:
        return Color{0.9F, 0.93F, 1.0F, 1.0F};
    case InfoBarSeverity::Warning:
        return Color{1.0F, 0.96F, 0.88F, 1.0F};
    case InfoBarSeverity::Error:
        return Color{1.0F, 0.91F, 0.9F, 1.0F};
    case InfoBarSeverity::Success:
        return Color{0.9F, 0.98F, 0.92F, 1.0F};
    }
    return Color{0.9F, 0.93F, 1.0F, 1.0F};
}

} // namespace

struct InfoBar::Impl {
    std::string message;
    InfoBarSeverity severity = InfoBarSeverity::Info;
    bool closable = true;
    Signal<> dismissed;
};

std::shared_ptr<InfoBar> InfoBar::create(std::string message, InfoBarSeverity severity) {
    return std::shared_ptr<InfoBar>(new InfoBar(std::move(message), severity));
}

InfoBar::InfoBar(std::string message, InfoBarSeverity severity) : impl_(std::make_unique<Impl>()) {
    impl_->message = std::move(message);
    impl_->severity = severity;
    add_style_class("info-bar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Label);
    accessible.set_name(impl_->message);
}

InfoBar::~InfoBar() = default;

std::string_view InfoBar::message() const {
    return impl_->message;
}

void InfoBar::set_message(std::string message) {
    if (impl_->message != message) {
        impl_->message = std::move(message);
        ensure_accessible().set_name(impl_->message);
        queue_layout();
        queue_redraw();
    }
}

InfoBarSeverity InfoBar::severity() const {
    return impl_->severity;
}

void InfoBar::set_severity(InfoBarSeverity severity) {
    if (impl_->severity != severity) {
        impl_->severity = severity;
        queue_redraw();
    }
}

bool InfoBar::is_closable() const {
    return impl_->closable;
}

void InfoBar::set_closable(bool closable) {
    if (impl_->closable != closable) {
        impl_->closable = closable;
        queue_layout();
        queue_redraw();
    }
}

Signal<>& InfoBar::on_dismissed() {
    return impl_->dismissed;
}

SizeRequest InfoBar::measure(const Constraints& /*constraints*/) const {
    const auto font = info_bar_font();
    const auto measured = measure_text(impl_->message, font);
    const float padding_x = theme_number("padding-x", 16.0F);
    const float close_width = impl_->closable ? theme_number("close-button-width", 24.0F) : 0.0F;
    const float min_height = theme_number("min-height", 40.0F);
    const float w = measured.width + (padding_x * 2.0F) + close_width;
    return {w, min_height, w, min_height};
}

bool InfoBar::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return false;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        return allocation().contains({event.x, event.y});
    case MouseEvent::Type::Release: {
        const auto a = allocation();
        const Point point{event.x, event.y};
        if (!a.contains(point)) {
            return false;
        }
        if (impl_->closable) {
            const float close_width = theme_number("close-button-width", 24.0F);
            if (event.x >= a.right() - close_width) {
                impl_->dismissed.emit();
                return true;
            }
        }
        return true;
    }
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool InfoBar::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Escape && impl_->closable) {
        impl_->dismissed.emit();
        return true;
    }

    return false;
}

void InfoBar::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 8.0F);
    const auto font = info_bar_font();

    // Background based on severity.
    const auto bg = background_for_severity(impl_->severity);
    ctx.add_rounded_rect(a, theme_color("background", bg), corner_radius);
    ctx.add_border(
        a, theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F}), 1.0F, corner_radius);

    // Message text.
    const float padding_x = theme_number("padding-x", 16.0F);
    const auto measured = measure_text(impl_->message, font);
    const float text_x = a.x + padding_x;
    const float text_y = a.y + std::max(0.0F, (a.height - measured.height) * 0.5F);
    ctx.add_text({text_x, text_y},
                 std::string(impl_->message),
                 theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F}),
                 font);

    // Close button.
    if (impl_->closable) {
        const float close_width = theme_number("close-button-width", 24.0F);
        const auto close_font = FontDescriptor{
            .family = {},
            .size = 16.0F,
            .weight = FontWeight::Regular,
        };
        const auto close_measured = measure_text("\xC3\x97", close_font); // "x" symbol
        const float close_x =
            a.right() - close_width + std::max(0.0F, (close_width - close_measured.width) * 0.5F);
        const float close_y = a.y + std::max(0.0F, (a.height - close_measured.height) * 0.5F);
        ctx.add_text({close_x, close_y},
                     "\xC3\x97",
                     theme_color("close-color", Color{0.3F, 0.3F, 0.3F, 1.0F}),
                     close_font);
    }
}

} // namespace nk
