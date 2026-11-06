#include "accessible_item.h"

#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/runtime/event_loop.h>
#include <nk/style/style_class.h>
#include <nk/style/theme.h>
#include <nk/widgets/search_bar.h>
#include <nk/widgets/search_field.h>
#include <utility>

namespace nk {

namespace {

constexpr float kButtonSize = 36.0F;
constexpr float kFieldWidth = 200.0F;
constexpr float kNarrowWidth = 280.0F;
constexpr float kCancelWidth = 28.0F;
constexpr auto kRevealDuration = std::chrono::milliseconds{160};
constexpr auto kRevealTick = std::chrono::milliseconds{16};

bool theme_requests_reduced_motion() {
    const auto theme = Theme::active();
    if (!theme) {
        return false;
    }
    const StyleValue* value = theme->token("motion-mode");
    return value != nullptr && std::holds_alternative<std::string>(*value) &&
           std::get<std::string>(*value) == "reduced";
}

void add_magnifier(SnapshotContext& ctx, Point center, Color color) {
    ctx.add_border({center.x - 5.0F, center.y - 6.0F, 8.0F, 8.0F}, color, 1.5F, 4.0F);
    ctx.add_line(
        {center.x + 1.5F, center.y + 1.0F}, {center.x + 5.0F, center.y + 4.5F}, color, 1.5F);
}

void add_cancel_mark(SnapshotContext& ctx, Rect bounds, Color color) {
    const Point center{bounds.x + (bounds.width * 0.5F), bounds.y + (bounds.height * 0.5F)};
    ctx.add_line(
        {center.x - 4.0F, center.y - 4.0F}, {center.x + 4.0F, center.y + 4.0F}, color, 1.5F);
    ctx.add_line(
        {center.x + 4.0F, center.y - 4.0F}, {center.x - 4.0F, center.y + 4.0F}, color, 1.5F);
}

} // namespace

struct SearchBar::Impl {
    explicit Impl(std::string placeholder) : field(SearchField::create(std::move(placeholder))) {}

    std::shared_ptr<SearchField> field;
    std::shared_ptr<detail::AccessibleItem> cancel;
    bool revealed = false;
    float reveal = 0.0F;
    float reveal_from = 0.0F;
    std::chrono::steady_clock::time_point reveal_started{};
    CallbackHandle reveal_timer{};
    bool button_armed = false;
    bool cancel_armed = false;
    Signal<bool> revealed_changed;
};

std::shared_ptr<SearchBar> SearchBar::create(std::string placeholder) {
    return std::shared_ptr<SearchBar>(new SearchBar(std::move(placeholder)));
}

SearchBar::SearchBar(std::string placeholder)
    : impl_(std::make_unique<Impl>(std::move(placeholder))) {
    set_focusable(true);
    add_style_class("search-bar");
    append_child(impl_->field);
    impl_->field->set_visible(false);
    impl_->cancel = detail::AccessibleItem::create("Cancel", AccessibleRole::Button, [this] {
        set_revealed(false);
        return true;
    });
    impl_->cancel->set_visible(false);
    impl_->cancel->ensure_accessible().set_hidden(true);
    append_child(impl_->cancel);
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Button);
    accessible.set_name("Search");
    accessible.add_action(AccessibleAction::Activate, [this] {
        if (impl_->revealed) {
            return false;
        }
        set_revealed(true);
        return true;
    });
}

SearchBar::~SearchBar() {
    cancel_reveal_timer();
}

std::string_view SearchBar::text() const {
    return impl_->field->text();
}

void SearchBar::set_text(std::string text) {
    impl_->field->set_text(std::move(text));
}

std::string_view SearchBar::placeholder() const {
    return impl_->field->placeholder();
}

void SearchBar::set_placeholder(std::string placeholder) {
    impl_->field->set_placeholder(std::move(placeholder));
}

bool SearchBar::is_revealed() const {
    return impl_->revealed;
}

void SearchBar::set_revealed(bool revealed) {
    if (impl_->revealed == revealed) {
        return;
    }
    impl_->revealed = revealed;
    if (revealed) {
        impl_->field->set_visible(true);
        impl_->field->grab_focus();
    } else {
        grab_focus();
    }
    const bool jump = prefers_reduced_motion() || is_narrow() || EventLoop::current() == nullptr;
    if (jump) {
        cancel_reveal_timer();
        impl_->reveal = revealed ? 1.0F : 0.0F;
        if (!revealed) {
            impl_->field->set_visible(false);
        }
    } else {
        start_reveal_animation();
    }
    layout_contents();
    impl_->revealed_changed.emit(revealed);
    queue_redraw();
}

Signal<std::string_view>& SearchBar::on_text_changed() {
    return impl_->field->on_text_changed();
}

Signal<std::string_view>& SearchBar::on_search() {
    return impl_->field->on_search();
}

Signal<bool>& SearchBar::on_revealed_changed() {
    return impl_->revealed_changed;
}

bool SearchBar::is_narrow() const {
    return allocation().width > 0.0F &&
           allocation().width < theme_number("narrow-width", kNarrowWidth);
}

bool SearchBar::prefers_reduced_motion() const {
    return theme_requests_reduced_motion();
}

float SearchBar::target_width() const {
    const float fixed = theme_number("field-width", kFieldWidth);
    const float available = allocation().width;
    if (available <= 0.0F) {
        return fixed;
    }
    if (is_narrow()) {
        return available;
    }
    return std::min(fixed, available);
}

float SearchBar::shown_width() const {
    const float button = theme_number("button-size", kButtonSize);
    const float target = target_width();
    return button + ((target - button) * std::clamp(impl_->reveal, 0.0F, 1.0F));
}

Rect SearchBar::cluster_rect() const {
    const auto bounds = allocation();
    const float width = std::min(shown_width(), std::max(0.0F, bounds.width));
    const float height =
        bounds.height > 0.0F ? bounds.height : theme_number("button-size", kButtonSize);
    return {bounds.right() - width, bounds.y, width, height};
}

Rect SearchBar::cancel_rect() const {
    if (impl_->reveal < 0.5F) {
        return {};
    }
    const auto cluster = cluster_rect();
    const float width = std::min(kCancelWidth, cluster.width);
    return {cluster.right() - width, cluster.y, width, cluster.height};
}

void SearchBar::layout_contents() {
    const auto cluster = cluster_rect();
    const auto cancel = cancel_rect();
    const float field_width = std::max(0.0F, cluster.width - cancel.width);
    impl_->field->allocate({cluster.x, cluster.y, field_width, cluster.height});
    impl_->cancel->allocate(cancel);
    const bool show_cancel =
        impl_->reveal >= 0.5F && (impl_->revealed || impl_->reveal_timer.valid());
    impl_->cancel->set_visible(show_cancel);
    impl_->cancel->ensure_accessible().set_hidden(!show_cancel);
    if (!impl_->revealed && impl_->reveal <= 0.0F) {
        impl_->field->set_visible(false);
    }
}

void SearchBar::cancel_reveal_timer() {
    if (!impl_->reveal_timer.valid()) {
        return;
    }
    if (auto* loop = EventLoop::current()) {
        loop->cancel(impl_->reveal_timer);
    }
    impl_->reveal_timer = {};
}

void SearchBar::start_reveal_animation() {
    cancel_reveal_timer();
    auto* loop = EventLoop::current();
    if (loop == nullptr || weak_from_this().expired()) {
        impl_->reveal = impl_->revealed ? 1.0F : 0.0F;
        return;
    }
    impl_->reveal_from = impl_->reveal;
    impl_->reveal_started = std::chrono::steady_clock::now();
    impl_->reveal_timer = loop->set_interval(
        kRevealTick,
        [weak = weak_from_this()] {
            auto widget = std::static_pointer_cast<SearchBar>(weak.lock());
            if (widget == nullptr) {
                return;
            }
            const float elapsed =
                std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() -
                                                         widget->impl_->reveal_started)
                    .count();
            const float t =
                std::clamp(elapsed / static_cast<float>(kRevealDuration.count()), 0.0F, 1.0F);
            const float target = widget->impl_->revealed ? 1.0F : 0.0F;
            widget->impl_->reveal =
                widget->impl_->reveal_from + ((target - widget->impl_->reveal_from) * t);
            if (t >= 1.0F) {
                widget->impl_->reveal = target;
                widget->cancel_reveal_timer();
                if (!widget->impl_->revealed) {
                    widget->impl_->field->set_visible(false);
                }
            }
            widget->layout_contents();
            widget->queue_redraw();
        },
        "search-bar-reveal");
}

SizeRequest SearchBar::measure(const Constraints& /*constraints*/) const {
    const float button = theme_number("button-size", kButtonSize);
    const float field = theme_number("field-width", kFieldWidth);
    const float natural = impl_->revealed ? field : button;
    return {button, button, natural, button};
}

void SearchBar::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    layout_contents();
}

bool SearchBar::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return false;
    }
    const Point point{event.x, event.y};
    if (impl_->reveal >= 0.5F) {
        const bool on_cancel = cancel_rect().contains(point);
        if (event.type == MouseEvent::Type::Press) {
            impl_->cancel_armed = on_cancel;
            return on_cancel;
        }
        if (event.type == MouseEvent::Type::Release) {
            const bool collapse = impl_->cancel_armed && on_cancel;
            impl_->cancel_armed = false;
            if (collapse) {
                set_revealed(false);
                return true;
            }
        }
        return false;
    }

    const bool on_button = cluster_rect().contains(point);
    if (event.type == MouseEvent::Type::Press) {
        impl_->button_armed = on_button;
        queue_redraw();
        return on_button;
    }
    if (event.type == MouseEvent::Type::Release) {
        const bool reveal = impl_->button_armed && on_button;
        impl_->button_armed = false;
        queue_redraw();
        if (reveal) {
            set_revealed(true);
        }
        return reveal;
    }
    return false;
}

bool SearchBar::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }
    if (impl_->revealed && event.key == KeyCode::Escape) {
        set_revealed(false);
        return true;
    }
    if (!impl_->revealed && (event.key == KeyCode::Return || event.key == KeyCode::Space)) {
        set_revealed(true);
        return true;
    }
    return false;
}

bool SearchBar::hit_test(Point point) const {
    return cluster_rect().contains(point);
}

CursorShape SearchBar::cursor_shape() const {
    return CursorShape::PointingHand;
}

void SearchBar::snapshot(SnapshotContext& ctx) const {
    const auto cluster = cluster_rect();
    const float radius = theme_number("corner-radius", 8.0F);
    const auto color = theme_color("text-color");
    if (impl_->reveal < 0.5F) {
        ctx.add_rounded_rect(
            cluster, theme_color("background", Color{0.94F, 0.95F, 0.97F, 1.0F}), radius);
        ctx.add_border(
            cluster, theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F}), 1.0F, radius);
        add_magnifier(
            ctx, {cluster.x + (cluster.width * 0.5F), cluster.y + (cluster.height * 0.5F)}, color);
        return;
    }

    Widget::snapshot(ctx);
    if (impl_->reveal >= 0.5F) {
        add_cancel_mark(ctx, cancel_rect(), color);
    }
}

} // namespace nk
