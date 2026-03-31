#include <nk/ui_core/widget.h>

#include <nk/controllers/event_controller.h>
#include <nk/layout/layout_manager.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/style/theme.h>
#include <nk/text/font.h>
#include <nk/text/text_shaper.h>

#include <algorithm>

namespace nk {

namespace {

StyleValue const* resolve_style_value(
    Theme const& theme,
    std::vector<std::string> const& classes,
    StateFlags state,
    std::string_view property_name) {

    auto const* value =
        theme.resolve({}, classes, state, property_name);
    if (value == nullptr) {
        value = theme.token(property_name);
    }

    // Allow properties to alias semantic tokens by string name.
    int depth = 0;
    while (value != nullptr && std::holds_alternative<std::string>(*value)
           && depth < 4) {
        value = theme.token(std::get<std::string>(*value));
        ++depth;
    }

    return value;
}

} // namespace

struct Widget::Impl {
    Widget* parent = nullptr;
    std::vector<std::shared_ptr<Widget>> children;
    std::vector<std::shared_ptr<EventController>> controllers;
    std::unique_ptr<LayoutManager> layout_manager;

    Rect allocation{};
    StateFlags state = StateFlags::None;
    std::vector<std::string> style_classes;

    bool visible = true;
    bool sensitive = true;
    bool focusable = false;
    Window* host_window = nullptr;

    Signal<> on_map;
    Signal<> on_unmap;
    Signal<> on_destroy;
};

Widget::Widget() : impl_(std::make_unique<Impl>()) {}

Widget::~Widget() {
    impl_->on_destroy.emit();
}

// --- Tree ---

Widget* Widget::parent() const { return impl_->parent; }

std::span<std::shared_ptr<Widget> const> Widget::children() const {
    return impl_->children;
}

// --- Visibility / Sensitivity ---

bool Widget::is_visible() const { return impl_->visible; }

void Widget::set_visible(bool visible) {
    if (impl_->visible != visible) {
        impl_->visible = visible;
        if (visible) {
            impl_->on_map.emit();
        } else {
            impl_->on_unmap.emit();
        }
        queue_layout();
    }
}

bool Widget::is_sensitive() const { return impl_->sensitive; }

void Widget::set_sensitive(bool sensitive) {
    if (impl_->sensitive != sensitive) {
        impl_->sensitive = sensitive;
        if (!sensitive) {
            set_state_flag(StateFlags::Disabled, true);
        } else {
            set_state_flag(StateFlags::Disabled, false);
        }
        queue_redraw();
    }
}

// --- State ---

StateFlags Widget::state_flags() const { return impl_->state; }

void Widget::set_state_flag(StateFlags flag, bool active) {
    auto previous = impl_->state;
    if (active) {
        impl_->state = impl_->state | flag;
    } else {
        impl_->state = impl_->state & ~flag;
    }
    if (impl_->state != previous) {
        queue_redraw();
    }
}

// --- Style classes ---

void Widget::add_style_class(std::string_view name) {
    auto const s = std::string(name);
    auto& classes = impl_->style_classes;
    if (std::find(classes.begin(), classes.end(), s) == classes.end()) {
        classes.push_back(s);
        queue_redraw();
    }
}

void Widget::remove_style_class(std::string_view name) {
    auto& classes = impl_->style_classes;
    auto const s = std::string(name);
    if (auto it = std::find(classes.begin(), classes.end(), s);
        it != classes.end()) {
        classes.erase(it);
        queue_redraw();
    }
}

bool Widget::has_style_class(std::string_view name) const {
    auto const& classes = impl_->style_classes;
    auto const s = std::string(name);
    return std::find(classes.begin(), classes.end(), s) != classes.end();
}

// --- Layout ---

SizeRequest Widget::measure(Constraints const& constraints) const {
    if (impl_->layout_manager) {
        return impl_->layout_manager->measure(*this, constraints);
    }
    return {};
}

void Widget::allocate(Rect const& allocation) {
    impl_->allocation = allocation;
    if (impl_->layout_manager) {
        impl_->layout_manager->allocate(
            *const_cast<Widget*>(this), allocation);
    }
}

Rect const& Widget::allocation() const { return impl_->allocation; }

void Widget::set_layout_manager(std::unique_ptr<LayoutManager> lm) {
    impl_->layout_manager = std::move(lm);
    queue_layout();
}

LayoutManager* Widget::layout_manager() const {
    return impl_->layout_manager.get();
}

// --- Focus ---

bool Widget::is_focusable() const { return impl_->focusable; }

void Widget::set_focusable(bool focusable) { impl_->focusable = focusable; }

void Widget::grab_focus() {
    set_state_flag(StateFlags::Focused, true);
    // TODO: notify focus manager
}

// --- Invalidation ---

void Widget::queue_redraw() {
    if (impl_->host_window) {
        impl_->host_window->request_frame();
    }
}

void Widget::queue_layout() {
    if (impl_->host_window) {
        impl_->host_window->invalidate_layout();
    }
}

// --- Controllers ---

void Widget::add_controller(std::shared_ptr<EventController> controller) {
    controller->set_widget(this);
    impl_->controllers.push_back(std::move(controller));
}

// --- Signals ---

Signal<>& Widget::on_map() { return impl_->on_map; }
Signal<>& Widget::on_unmap() { return impl_->on_unmap; }
Signal<>& Widget::on_destroy() { return impl_->on_destroy; }

// --- Child management ---

void Widget::append_child(std::shared_ptr<Widget> child) {
    child->impl_->parent = this;
    child->set_host_window(impl_->host_window);
    impl_->children.push_back(std::move(child));
    queue_layout();
}

void Widget::insert_child(std::size_t index, std::shared_ptr<Widget> child) {
    child->impl_->parent = this;
    child->set_host_window(impl_->host_window);
    auto pos = impl_->children.begin()
               + static_cast<std::ptrdiff_t>(
                     std::min(index, impl_->children.size()));
    impl_->children.insert(pos, std::move(child));
    queue_layout();
}

void Widget::remove_child(Widget& child) {
    auto& c = impl_->children;
    auto it = std::find_if(c.begin(), c.end(),
                           [&](auto const& p) { return p.get() == &child; });
    if (it != c.end()) {
        (*it)->set_host_window(nullptr);
        (*it)->impl_->parent = nullptr;
        c.erase(it);
        queue_layout();
    }
}

void Widget::clear_children() {
    for (auto& ch : impl_->children) {
        ch->set_host_window(nullptr);
        ch->impl_->parent = nullptr;
    }
    impl_->children.clear();
    queue_layout();
}

void Widget::set_host_window(Window* window) {
    impl_->host_window = window;
    for (auto& child : impl_->children) {
        if (child) {
            child->set_host_window(window);
        }
    }
}

Color Widget::theme_color(
    std::string_view property_name,
    Color fallback) const {
    auto theme = Theme::active();
    if (!theme) {
        return fallback;
    }

    auto const* value = resolve_style_value(
        *theme, impl_->style_classes, impl_->state, property_name);
    if (value != nullptr && std::holds_alternative<Color>(*value)) {
        return std::get<Color>(*value);
    }

    return fallback;
}

float Widget::theme_number(
    std::string_view property_name,
    float fallback) const {
    auto theme = Theme::active();
    if (!theme) {
        return fallback;
    }

    auto const* value = resolve_style_value(
        *theme, impl_->style_classes, impl_->state, property_name);
    if (value != nullptr && std::holds_alternative<float>(*value)) {
        return std::get<float>(*value);
    }

    return fallback;
}

void Widget::snapshot(SnapshotContext& ctx) const {
    // Default: recursively snapshot visible children.
    for (auto const& child : impl_->children) {
        if (child && child->is_visible()) {
            child->snapshot(ctx);
        }
    }
}

bool Widget::handle_mouse_event(MouseEvent const& /*event*/) {
    return false;
}

bool Widget::handle_key_event(KeyEvent const& /*event*/) {
    return false;
}

bool Widget::hit_test(Point point) const {
    return allocation().contains(point);
}

void Widget::on_focus_changed(bool /*focused*/) {}

Size Widget::measure_text(
    std::string_view text,
    FontDescriptor const& font) const {
    if (text.empty()) {
        return {};
    }

    if (impl_->host_window != nullptr
        && impl_->host_window->text_shaper() != nullptr) {
        return impl_->host_window->text_shaper()->measure(text, font);
    }

    auto const length = static_cast<float>(text.size());
    return {length * font.size * 0.55F, font.size * 1.35F};
}

} // namespace nk
