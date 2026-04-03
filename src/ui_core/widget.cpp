#include <algorithm>
#include <nk/controllers/event_controller.h>
#include <nk/layout/layout_manager.h>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/style/theme.h>
#include <nk/text/font.h>
#include <nk/text/text_shaper.h>
#include <nk/ui_core/widget.h>

namespace nk {

namespace {

const StyleValue* resolve_style_value(const Theme& theme,
                                      const std::vector<std::string>& classes,
                                      StateFlags state,
                                      std::string_view property_name) {

    const auto* value = theme.resolve({}, classes, state, property_name);
    if (value == nullptr) {
        value = theme.token(property_name);
    }

    // Allow properties to alias semantic tokens by string name.
    int depth = 0;
    while (value != nullptr && std::holds_alternative<std::string>(*value) && depth < 4) {
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
    Rect previous_allocation{};
    StateFlags state = StateFlags::None;
    std::vector<std::string> style_classes;
    std::string debug_name;

    bool visible = true;
    bool sensitive = true;
    bool focusable = false;
    bool has_allocation = false;
    bool has_previous_allocation = false;
    bool retain_size_when_hidden = false;
    SizePolicy horizontal_size_policy = SizePolicy::Preferred;
    SizePolicy vertical_size_policy = SizePolicy::Preferred;
    uint8_t horizontal_stretch = 0;
    uint8_t vertical_stretch = 0;
    Window* host_window = nullptr;
    std::unique_ptr<Accessible> accessible;
    WidgetHotspotCounters debug_hotspot_counters;
    bool debug_pending_redraw = false;
    bool debug_pending_layout = false;
    bool debug_has_last_measure = false;
    Constraints debug_last_measure_constraints{};
    SizeRequest debug_last_size_request{};
    std::vector<Rect> preserved_damage_regions;

    Signal<> on_map;
    Signal<> on_unmap;
    Signal<> on_destroy;
};

Widget::Widget() : impl_(std::make_unique<Impl>()) {}

Widget::~Widget() {
    impl_->on_destroy.emit();
}

// --- Tree ---

Widget* Widget::parent() const {
    return impl_->parent;
}

std::span<const std::shared_ptr<Widget>> Widget::children() const {
    return impl_->children;
}

// --- Visibility / Sensitivity ---

bool Widget::is_visible() const {
    return impl_->visible;
}

void Widget::set_visible(bool visible) {
    if (impl_->visible != visible) {
        impl_->visible = visible;
        if (visible) {
            impl_->on_map.emit();
        } else {
            impl_->on_unmap.emit();
            if (impl_->host_window != nullptr) {
                impl_->host_window->handle_widget_state_change(*this);
            }
        }
        sync_accessible_state();
        queue_layout();
    }
}

bool Widget::is_sensitive() const {
    return impl_->sensitive;
}

void Widget::set_sensitive(bool sensitive) {
    if (impl_->sensitive != sensitive) {
        impl_->sensitive = sensitive;
        if (!sensitive) {
            set_state_flag(StateFlags::Disabled, true);
            if (impl_->host_window != nullptr) {
                impl_->host_window->handle_widget_state_change(*this);
            }
        } else {
            set_state_flag(StateFlags::Disabled, false);
        }
        sync_accessible_state();
        queue_redraw();
    }
}

// --- State ---

StateFlags Widget::state_flags() const {
    return impl_->state;
}

void Widget::set_state_flag(StateFlags flag, bool active) {
    auto previous = impl_->state;
    if (active) {
        impl_->state = impl_->state | flag;
    } else {
        impl_->state = impl_->state & ~flag;
    }
    if (impl_->state != previous) {
        sync_accessible_state();
        queue_redraw();
    }
}

// --- Style classes ---

void Widget::add_style_class(std::string_view name) {
    const auto s = std::string(name);
    auto& classes = impl_->style_classes;
    if (std::find(classes.begin(), classes.end(), s) == classes.end()) {
        classes.push_back(s);
        queue_redraw();
    }
}

void Widget::remove_style_class(std::string_view name) {
    auto& classes = impl_->style_classes;
    const auto s = std::string(name);
    if (auto it = std::find(classes.begin(), classes.end(), s); it != classes.end()) {
        classes.erase(it);
        queue_redraw();
    }
}

bool Widget::has_style_class(std::string_view name) const {
    const auto& classes = impl_->style_classes;
    const auto s = std::string(name);
    return std::find(classes.begin(), classes.end(), s) != classes.end();
}

std::span<const std::string> Widget::style_classes() const {
    return impl_->style_classes;
}

void Widget::set_debug_name(std::string_view name) {
    const auto value = std::string(name);
    if (impl_->debug_name == value) {
        return;
    }
    impl_->debug_name = value;
}

std::string_view Widget::debug_name() const {
    return impl_->debug_name;
}

// --- Layout ---

SizeRequest Widget::measure(const Constraints& constraints) const {
    if (impl_->layout_manager) {
        return impl_->layout_manager->measure(*this, constraints);
    }
    return {};
}

SizeRequest Widget::measure_for_diagnostics(const Constraints& constraints) const {
    ++impl_->debug_hotspot_counters.measure_count;
    const auto request = measure(constraints);
    impl_->debug_has_last_measure = true;
    impl_->debug_last_measure_constraints = constraints;
    impl_->debug_last_size_request = request;
    return request;
}

void Widget::allocate(const Rect& allocation) {
    ++impl_->debug_hotspot_counters.allocate_count;
    if (impl_->has_allocation) {
        preserve_damage_regions_for_next_redraw();
        impl_->previous_allocation = impl_->allocation;
        impl_->has_previous_allocation = true;
    }
    impl_->allocation = allocation;
    impl_->has_allocation = true;
    impl_->debug_pending_layout = false;
    if (impl_->layout_manager) {
        impl_->layout_manager->allocate(*const_cast<Widget*>(this), allocation);
    }
}

const Rect& Widget::allocation() const {
    return impl_->allocation;
}

void Widget::set_layout_manager(std::unique_ptr<LayoutManager> lm) {
    impl_->layout_manager = std::move(lm);
    queue_layout();
}

LayoutManager* Widget::layout_manager() const {
    return impl_->layout_manager.get();
}

SizePolicy Widget::horizontal_size_policy() const {
    return impl_->horizontal_size_policy;
}

void Widget::set_horizontal_size_policy(SizePolicy policy) {
    if (impl_->horizontal_size_policy == policy) {
        return;
    }
    impl_->horizontal_size_policy = policy;
    queue_layout();
}

SizePolicy Widget::vertical_size_policy() const {
    return impl_->vertical_size_policy;
}

void Widget::set_vertical_size_policy(SizePolicy policy) {
    if (impl_->vertical_size_policy == policy) {
        return;
    }
    impl_->vertical_size_policy = policy;
    queue_layout();
}

uint8_t Widget::horizontal_stretch() const {
    return impl_->horizontal_stretch;
}

void Widget::set_horizontal_stretch(uint8_t stretch) {
    if (impl_->horizontal_stretch == stretch) {
        return;
    }
    impl_->horizontal_stretch = stretch;
    queue_layout();
}

uint8_t Widget::vertical_stretch() const {
    return impl_->vertical_stretch;
}

void Widget::set_vertical_stretch(uint8_t stretch) {
    if (impl_->vertical_stretch == stretch) {
        return;
    }
    impl_->vertical_stretch = stretch;
    queue_layout();
}

bool Widget::retain_size_when_hidden() const {
    return impl_->retain_size_when_hidden;
}

void Widget::set_retain_size_when_hidden(bool retain) {
    if (impl_->retain_size_when_hidden == retain) {
        return;
    }
    impl_->retain_size_when_hidden = retain;
    queue_layout();
}

// --- Focus ---

bool Widget::is_focusable() const {
    return impl_->focusable;
}

void Widget::set_focusable(bool focusable) {
    if (impl_->focusable == focusable) {
        return;
    }
    impl_->focusable = focusable;
    if (impl_->accessible != nullptr) {
        if (focusable) {
            impl_->accessible->add_action(AccessibleAction::Focus, [this]() {
                grab_focus();
                return true;
            });
        } else {
            impl_->accessible->remove_action(AccessibleAction::Focus);
        }
    }
}

void Widget::grab_focus() {
    if (impl_->host_window != nullptr) {
        impl_->host_window->focus_widget(this);
        return;
    }
    set_state_flag(StateFlags::Focused, true);
    dispatch_focus_controllers(true);
    on_focus_changed(true);
}

Accessible* Widget::accessible() {
    return impl_->accessible.get();
}

const Accessible* Widget::accessible() const {
    return impl_->accessible.get();
}

Accessible& Widget::ensure_accessible() {
    if (!impl_->accessible) {
        impl_->accessible = std::make_unique<Accessible>();
        if (impl_->focusable) {
            impl_->accessible->add_action(AccessibleAction::Focus, [this]() {
                grab_focus();
                return true;
            });
        }
    }
    sync_accessible_state();
    return *impl_->accessible;
}

WidgetHotspotCounters Widget::debug_hotspot_counters() const {
    return impl_->debug_hotspot_counters;
}

bool Widget::debug_pending_redraw() const {
    return impl_->debug_pending_redraw;
}

bool Widget::debug_pending_layout() const {
    return impl_->debug_pending_layout;
}

bool Widget::debug_has_last_measure() const {
    return impl_->debug_has_last_measure;
}

Constraints Widget::debug_last_measure_constraints() const {
    return impl_->debug_last_measure_constraints;
}

SizeRequest Widget::debug_last_size_request() const {
    return impl_->debug_last_size_request;
}

std::vector<Rect> Widget::debug_damage_regions() const {
    return damage_regions();
}

std::span<const Rect> Widget::debug_preserved_damage_regions() const {
    return impl_->preserved_damage_regions;
}

bool Widget::debug_has_previous_allocation() const {
    return impl_->has_previous_allocation;
}

Rect Widget::debug_previous_allocation() const {
    return impl_->previous_allocation;
}

// --- Invalidation ---

void Widget::queue_redraw() {
    impl_->debug_pending_redraw = true;
    if (impl_->host_window) {
        impl_->host_window->note_widget_redraw_request(*this);
    }
}

void Widget::queue_layout() {
    impl_->debug_pending_layout = true;
    if (impl_->host_window) {
        impl_->host_window->note_widget_layout_request(*this);
    }
}

// --- Controllers ---

void Widget::add_controller(std::shared_ptr<EventController> controller) {
    controller->set_widget(this);
    impl_->controllers.push_back(std::move(controller));
}

void Widget::dispatch_pointer_controllers(const MouseEvent& event) {
    for (const auto& controller : impl_->controllers) {
        auto* pointer = dynamic_cast<PointerController*>(controller.get());
        if (pointer == nullptr) {
            continue;
        }

        switch (event.type) {
        case MouseEvent::Type::Enter:
            pointer->on_enter().emit(event.x, event.y);
            break;
        case MouseEvent::Type::Leave:
            pointer->on_leave().emit();
            break;
        case MouseEvent::Type::Move:
            pointer->on_motion().emit(event.x, event.y);
            break;
        case MouseEvent::Type::Press:
            pointer->on_pressed().emit(event.x, event.y, event.button);
            break;
        case MouseEvent::Type::Release:
            pointer->on_released().emit(event.x, event.y, event.button);
            break;
        case MouseEvent::Type::Scroll:
            break;
        }
    }
}

void Widget::dispatch_keyboard_controllers(const KeyEvent& event) {
    for (const auto& controller : impl_->controllers) {
        auto* keyboard = dynamic_cast<KeyboardController*>(controller.get());
        if (keyboard == nullptr) {
            continue;
        }

        if (event.type == KeyEvent::Type::Press) {
            keyboard->on_key_pressed().emit(static_cast<int>(event.key),
                                            static_cast<int>(event.modifiers));
        } else {
            keyboard->on_key_released().emit(static_cast<int>(event.key),
                                             static_cast<int>(event.modifiers));
        }
    }
}

void Widget::dispatch_focus_controllers(bool focused) {
    for (const auto& controller : impl_->controllers) {
        auto* focus = dynamic_cast<FocusController*>(controller.get());
        if (focus == nullptr) {
            continue;
        }

        if (focused) {
            focus->on_focus_in().emit();
        } else {
            focus->on_focus_out().emit();
        }
    }
}

void Widget::sync_accessible_state() {
    if (!impl_->accessible) {
        return;
    }
    impl_->accessible->set_hidden(!impl_->visible);
    impl_->accessible->set_state(impl_->state);
}

// --- Signals ---

Signal<>& Widget::on_map() {
    return impl_->on_map;
}

Signal<>& Widget::on_unmap() {
    return impl_->on_unmap;
}

Signal<>& Widget::on_destroy() {
    return impl_->on_destroy;
}

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
    auto pos = impl_->children.begin() +
               static_cast<std::ptrdiff_t>(std::min(index, impl_->children.size()));
    impl_->children.insert(pos, std::move(child));
    queue_layout();
}

void Widget::remove_child(Widget& child) {
    auto& c = impl_->children;
    auto it = std::find_if(c.begin(), c.end(), [&](const auto& p) { return p.get() == &child; });
    if (it != c.end()) {
        if ((*it)->impl_->host_window != nullptr) {
            (*it)->impl_->host_window->handle_widget_detached(*(*it));
        }
        (*it)->set_host_window(nullptr);
        (*it)->impl_->parent = nullptr;
        c.erase(it);
        queue_layout();
    }
}

void Widget::clear_children() {
    for (auto& ch : impl_->children) {
        if (ch->impl_->host_window != nullptr) {
            ch->impl_->host_window->handle_widget_detached(*ch);
        }
        ch->set_host_window(nullptr);
        ch->impl_->parent = nullptr;
    }
    impl_->children.clear();
    queue_layout();
}

void Widget::set_host_window(Window* window) {
    if (window == nullptr && impl_->host_window != nullptr) {
        impl_->host_window->handle_widget_detached(*this);
    }
    impl_->host_window = window;
    for (auto& child : impl_->children) {
        if (child) {
            child->set_host_window(window);
        }
    }
}

Color Widget::theme_color(std::string_view property_name, Color fallback) const {
    auto theme = Theme::active();
    if (!theme) {
        return fallback;
    }

    const auto* value =
        resolve_style_value(*theme, impl_->style_classes, impl_->state, property_name);
    if (value != nullptr && std::holds_alternative<Color>(*value)) {
        return std::get<Color>(*value);
    }

    return fallback;
}

float Widget::theme_number(std::string_view property_name, float fallback) const {
    auto theme = Theme::active();
    if (!theme) {
        return fallback;
    }

    const auto* value =
        resolve_style_value(*theme, impl_->style_classes, impl_->state, property_name);
    if (value != nullptr && std::holds_alternative<float>(*value)) {
        return std::get<float>(*value);
    }

    return fallback;
}

void Widget::snapshot(SnapshotContext& ctx) const {
    // Default: recursively snapshot visible children.
    for (const auto& child : impl_->children) {
        if (child && child->is_visible()) {
            child->snapshot_subtree(ctx);
        }
    }
}

std::vector<std::size_t> Widget::debug_tree_path() const {
    std::vector<std::size_t> path;
    for (auto* current = this; current != nullptr && current->impl_->parent != nullptr;
         current = current->impl_->parent) {
        const auto& siblings = current->impl_->parent->impl_->children;
        const auto it =
            std::find_if(siblings.begin(), siblings.end(), [current](const auto& child) {
                return child.get() == current;
            });
        if (it == siblings.end()) {
            break;
        }
        path.push_back(static_cast<std::size_t>(it - siblings.begin()));
    }
    std::reverse(path.begin(), path.end());
    return path;
}

std::string Widget::debug_snapshot_label() const {
    return impl_->debug_name.empty() ? "widget" : impl_->debug_name;
}

void Widget::snapshot_subtree(SnapshotContext& ctx) const {
    ++impl_->debug_hotspot_counters.snapshot_count;
    const auto path = debug_tree_path();
    ctx.push_debug_source(debug_snapshot_label(), path);
    snapshot(ctx);
    ctx.pop_debug_source();
    impl_->debug_pending_redraw = false;
}

void Widget::reset_debug_hotspot_counters_recursive() {
    impl_->debug_hotspot_counters = {};
    for (const auto& child : impl_->children) {
        if (child != nullptr) {
            child->reset_debug_hotspot_counters_recursive();
        }
    }
}

void Widget::clear_preserved_damage_regions() {
    impl_->preserved_damage_regions.clear();
}

bool Widget::handle_mouse_event(const MouseEvent& /*event*/) {
    return false;
}

bool Widget::handle_key_event(const KeyEvent& /*event*/) {
    return false;
}

bool Widget::hit_test(Point point) const {
    return allocation().contains(point);
}

std::vector<Rect> Widget::damage_regions() const {
    return {allocation()};
}

CursorShape Widget::cursor_shape() const {
    return CursorShape::Default;
}

void Widget::on_focus_changed(bool /*focused*/) {}

Size Widget::measure_text(std::string_view text, const FontDescriptor& font) const {
    if (text.empty()) {
        return {};
    }

    note_text_measure_for_diagnostics();

    if (impl_->host_window != nullptr && impl_->host_window->text_shaper() != nullptr) {
        return impl_->host_window->text_shaper()->measure(text, font);
    }

    const auto length = static_cast<float>(text.size());
    return {length * font.size * 0.55F, font.size * 1.35F};
}

void Widget::note_text_measure_for_diagnostics() const {
    ++impl_->debug_hotspot_counters.text_measure_count;
}

void Widget::note_image_snapshot_for_diagnostics() const {
    ++impl_->debug_hotspot_counters.image_snapshot_count;
}

void Widget::note_model_view_sync_for_diagnostics(std::size_t materialized_rows,
                                                  std::size_t reused_rows,
                                                  std::size_t disposed_rows) const {
    ++impl_->debug_hotspot_counters.model_sync_count;
    impl_->debug_hotspot_counters.model_row_materialize_count += materialized_rows;
    impl_->debug_hotspot_counters.model_row_reuse_count += reused_rows;
    impl_->debug_hotspot_counters.model_row_dispose_count += disposed_rows;
}

void Widget::preserve_damage_regions_for_next_redraw() {
    impl_->preserved_damage_regions = damage_regions();
}

} // namespace nk
