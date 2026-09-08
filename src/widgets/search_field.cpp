#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/search_field.h>

namespace nk {

struct SearchField::Impl {
    Signal<std::string_view> search;
    ScopedConnection text_changed;
    ScopedConnection activated;
    bool clear_armed = false;
};

std::shared_ptr<SearchField> SearchField::create(std::string placeholder) {
    return std::shared_ptr<SearchField>(new SearchField(std::move(placeholder)));
}

SearchField::SearchField(std::string placeholder) : TextField({}), impl_(std::make_unique<Impl>()) {
    remove_style_class("text-field");
    add_style_class("search-field");
    set_placeholder(std::move(placeholder));
    impl_->text_changed =
        ScopedConnection(TextField::on_text_changed().connect([this](std::string_view value) {
            ensure_accessible().set_value(std::string(value));
            // The clear control appears/disappears with the query, outside the
            // editor's localized text damage region.
            queue_redraw();
        }));
    impl_->activated =
        ScopedConnection(TextField::on_activate().connect([this] { impl_->search.emit(text()); }));
    auto& accessible = ensure_accessible();
    accessible.add_action(AccessibleAction::Focus, [this] {
        grab_focus();
        return true;
    });
    accessible.add_action(AccessibleAction::Activate, [this] {
        return handle_key_event({.type = KeyEvent::Type::Press, .key = KeyCode::Return});
    });
}

SearchField::~SearchField() = default;

std::string_view SearchField::text() const {
    return TextField::text();
}

void SearchField::set_text(std::string text) {
    TextField::set_text(std::move(text));
}

std::string_view SearchField::placeholder() const {
    return TextField::placeholder();
}

void SearchField::set_placeholder(std::string placeholder) {
    TextField::set_placeholder(std::move(placeholder));
    ensure_accessible().set_name(this->placeholder().empty() ? "Search"
                                                             : std::string(this->placeholder()));
}

Signal<std::string_view>& SearchField::on_text_changed() {
    return TextField::on_text_changed();
}

Signal<std::string_view>& SearchField::on_search() {
    return impl_->search;
}

SizeRequest SearchField::measure(const Constraints& constraints) const {
    auto request = TextField::measure(constraints);
    request.natural_width = theme_number("min-width", 200.0F);
    return request;
}

Rect SearchField::clear_button_rect() const {
    const auto body = inner_body_rect();
    const float width = std::clamp(theme_number("clear-button-width", 28.0F), 0.0F, body.width);
    return {body.right() - width, body.y, width, body.height};
}

Rect SearchField::text_rect() const {
    const auto body = inner_body_rect();
    const float icon_width = std::clamp(theme_number("icon-width", 28.0F), 0.0F, body.width);
    const float trailing = !text().empty() && is_editable() ? clear_button_rect().width : 12.0F;
    return {body.x + icon_width,
            body.y,
            std::max(0.0F, body.width - icon_width - trailing),
            body.height};
}

bool SearchField::clear_query() {
    if (!is_editable() || text().empty()) {
        return false;
    }
    select_all();
    return TextField::handle_key_event({.type = KeyEvent::Type::Press, .key = KeyCode::Backspace});
}

bool SearchField::handle_mouse_event(const MouseEvent& event) {
    const auto point = Point{event.x, event.y};
    if (event.type == MouseEvent::Type::Press && event.button == 1 &&
        allocation().contains(point)) {
        grab_focus();
        if (is_editable() && !text().empty() && clear_button_rect().contains(point)) {
            impl_->clear_armed = true;
            return true;
        }
    }
    if (impl_->clear_armed) {
        if (event.type == MouseEvent::Type::Release && event.button == 1) {
            impl_->clear_armed = false;
            if (clear_button_rect().contains(point)) {
                clear_query();
            }
            return true;
        }
        if (event.type == MouseEvent::Type::Move) {
            return true;
        }
    }
    return TextField::handle_mouse_event(event);
}

bool SearchField::handle_key_event(const KeyEvent& event) {
    if (event.type == KeyEvent::Type::Press && event.key == KeyCode::Escape) {
        if (has_preedit()) {
            clear_preedit();
            return true;
        }
        return clear_query();
    }
    return TextField::handle_key_event(event);
}

bool SearchField::handle_text_input_event(const TextInputEvent& event) {
    return TextField::handle_text_input_event(event);
}

CursorShape SearchField::cursor_shape() const {
    return TextField::cursor_shape();
}

void SearchField::on_focus_changed(bool focused) {
    impl_->clear_armed = false;
    TextField::on_focus_changed(focused);
    queue_redraw();
}

void SearchField::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const auto body = inner_body_rect();
    const float corner_radius = theme_number("corner-radius", 10.0F);
    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a, theme_color("focus-ring-color"), corner_radius + 2.0F);
    }
    ctx.add_rounded_rect(
        body, theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}), 1.0F, corner_radius);

    ctx.push_rounded_clip(body, corner_radius);
    const auto icon_color = theme_color("icon-color", theme_color("text-color"));
    const float icon_width = std::clamp(theme_number("icon-width", 28.0F), 0.0F, body.width);
    const float center_x = body.x + icon_width * 0.5F;
    const float center_y = body.y + body.height * 0.5F;
    ctx.add_border({center_x - 6.0F, center_y - 6.0F, 10.0F, 10.0F}, icon_color, 1.5F, 5.0F);
    ctx.add_line(
        {center_x + 2.5F, center_y + 2.5F}, {center_x + 6.0F, center_y + 6.0F}, icon_color, 1.5F);
    snapshot_text(ctx);

    if (!text().empty() && is_editable()) {
        const auto clear = clear_button_rect();
        const float x = clear.x + clear.width * 0.5F;
        const float y = clear.y + clear.height * 0.5F;
        ctx.add_line({x - 3.0F, y - 3.0F}, {x + 3.0F, y + 3.0F}, icon_color, 1.5F);
        ctx.add_line({x + 3.0F, y - 3.0F}, {x - 3.0F, y + 3.0F}, icon_color, 1.5F);
    }
    ctx.pop_container();
}

} // namespace nk
