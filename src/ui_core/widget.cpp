#include <algorithm>
#include <cassert>
#include <nk/controllers/event_controller.h>
#include <nk/layout/layout_manager.h>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/style/theme.h>
#include <nk/style/theme_defaults.h>
#include <nk/text/font.h>
#include <nk/text/text_shaper.h>
#include <nk/ui_core/widget.h>

namespace nk {

namespace {

/// Try widget-level rule resolution only (no global token fallback).
/// `min_specificity` = 1 skips the selector-less base defaults so a parent's
/// class-specific value can be inherited in preference to the generic default.
const StyleValue* resolve_widget_rule(const Theme& theme,
                                      const std::vector<std::string>& classes,
                                      StateFlags state,
                                      std::string_view property_name,
                                      int min_specificity = 0) {
    const auto* value = theme.resolve({}, classes, state, property_name, min_specificity);

    // Allow properties to alias semantic tokens by string name.
    int depth = 0;
    while (value != nullptr && std::holds_alternative<std::string>(*value) && depth < 4) {
        value = theme.token(std::get<std::string>(*value));
        ++depth;
    }

    return value;
}

/// Properties that cascade from parent to child when the child has no
/// explicit value set by a style rule. Modeled after CSS inherited
/// properties — text-related properties inherit; box/visual properties
/// (background, border, corner-radius) do not.
bool is_inheritable_property(std::string_view name) {
    return name == "text-color" || name == "font-family" || name == "font-size" ||
           name == "font-weight";
}

[[nodiscard]] bool rect_is_empty(Rect rect) {
    return rect.width <= 0.0F || rect.height <= 0.0F;
}

[[nodiscard]] bool rects_overlap_or_touch(Rect lhs, Rect rhs) {
    return lhs.x <= rhs.right() && rhs.x <= lhs.right() && lhs.y <= rhs.bottom() &&
           rhs.y <= lhs.bottom();
}

[[nodiscard]] Rect union_rect(Rect lhs, Rect rhs) {
    const float x0 = std::min(lhs.x, rhs.x);
    const float y0 = std::min(lhs.y, rhs.y);
    const float x1 = std::max(lhs.right(), rhs.right());
    const float y1 = std::max(lhs.bottom(), rhs.bottom());
    return {x0, y0, std::max(0.0F, x1 - x0), std::max(0.0F, y1 - y0)};
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
    Insets margin{};
    Insets padding{};
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
    std::vector<Rect> queued_redraw_regions;
    std::vector<Rect> preserved_damage_regions;

    Signal<> on_map;
    Signal<> on_unmap;
    Signal<> on_destroy;
    Signal<DragDropEvent&> on_drag_enter;
    Signal<DragDropEvent&> on_drag_motion;
    Signal<DragDropEvent&> on_drag_leave;
    Signal<DragDropEvent&> on_drop;
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

bool Widget::has_height_for_width() const {
    if (impl_->layout_manager) {
        return impl_->layout_manager->has_height_for_width(*this);
    }
    return false;
}

float Widget::height_for_width(float width) const {
    if (impl_->layout_manager) {
        return impl_->layout_manager->height_for_width(*this, width);
    }
    return measure(Constraints::unbounded()).natural_height;
}

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

// --- Margin and padding ---

Insets Widget::margin() const {
    return impl_->margin;
}

void Widget::set_margin(Insets margin) {
    if (impl_->margin == margin) {
        return;
    }
    impl_->margin = margin;
    queue_layout();
}

Insets Widget::padding() const {
    return impl_->padding;
}

void Widget::set_padding(Insets padding) {
    if (impl_->padding == padding) {
        return;
    }
    impl_->padding = padding;
    queue_layout();
    queue_redraw();
}

Rect Widget::content_rect() const {
    const auto& a = impl_->allocation;
    const auto& p = impl_->padding;
    return {
        a.x + p.left,
        a.y + p.top,
        std::max(0.0F, a.width - p.left - p.right),
        std::max(0.0F, a.height - p.top - p.bottom),
    };
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
    impl_->queued_redraw_regions.clear();
    if (impl_->host_window) {
        impl_->host_window->note_widget_redraw_request(*this);
    }
}

void Widget::queue_redraw(Rect damage) {
    if (rect_is_empty(damage)) {
        return;
    }

    bool merged = false;
    for (auto& existing : impl_->queued_redraw_regions) {
        if (rects_overlap_or_touch(existing, damage)) {
            existing = union_rect(existing, damage);
            merged = true;
            break;
        }
    }
    if (!merged) {
        impl_->queued_redraw_regions.push_back(damage);
    }

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
        case MouseEvent::Type::DragStart:
        case MouseEvent::Type::DragUpdate:
        case MouseEvent::Type::DragEnd:
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

Signal<DragDropEvent&>& Widget::on_drag_enter() {
    return impl_->on_drag_enter;
}

Signal<DragDropEvent&>& Widget::on_drag_motion() {
    return impl_->on_drag_motion;
}

Signal<DragDropEvent&>& Widget::on_drag_leave() {
    return impl_->on_drag_leave;
}

Signal<DragDropEvent&>& Widget::on_drop() {
    return impl_->on_drop;
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

    // 1. Try this widget's own rules. For inheritable properties the
    //    selector-less base defaults are deferred so a parent's class-specific
    //    value can still cascade in preference to the generic default.
    const bool inheritable = is_inheritable_property(property_name);
    const auto* value = resolve_widget_rule(
        *theme, impl_->style_classes, impl_->state, property_name, inheritable ? 1 : 0);

    // 2. Inheritable properties cascade from parent to child, then fall back
    //    to this widget's base-rule default.
    if (value == nullptr && inheritable) {
        for (const Widget* p = parent(); p != nullptr; p = p->parent()) {
            value = resolve_widget_rule(
                *theme, p->impl_->style_classes, p->impl_->state, property_name, 1);
            if (value != nullptr) {
                break;
            }
        }
        if (value == nullptr) {
            value = resolve_widget_rule(*theme, impl_->style_classes, impl_->state, property_name);
        }
    }

    // 3. Global token fallback.
    if (value == nullptr) {
        value = theme->token(property_name);
        int depth = 0;
        while (value != nullptr && std::holds_alternative<std::string>(*value) && depth < 4) {
            value = theme->token(std::get<std::string>(*value));
            ++depth;
        }
    }

    if (value != nullptr && std::holds_alternative<Color>(*value)) {
        return std::get<Color>(*value);
    }
    return fallback;
}

Color Widget::theme_color(std::string_view property_name) const {
    for (const auto& entry : ThemeColorDefaults) {
        if (entry.property != property_name) {
            continue;
        }
        // Prefer the entry's semantic token resolved against the active theme,
        // so a property no rule covers still follows the current palette
        // (dark, high contrast, accent override). The constexpr light value is
        // only the last-resort safety net for a theme missing the token.
        if (auto theme = Theme::active()) {
            const StyleValue* value = theme->token(entry.token);
            int depth = 0;
            while (value != nullptr && std::holds_alternative<std::string>(*value) && depth < 4) {
                value = theme->token(std::get<std::string>(*value));
                ++depth;
            }
            if (value != nullptr && std::holds_alternative<Color>(*value)) {
                return theme_color(property_name, std::get<Color>(*value));
            }
        }
        return theme_color(property_name, entry.value);
    }
    assert(false && "property is missing from ThemeColorDefaults");
    return theme_color(property_name, Color{});
}

float Widget::theme_number(std::string_view property_name, float fallback) const {
    auto theme = Theme::active();
    if (!theme) {
        return fallback;
    }

    // 1. Try this widget's own rules. For inheritable properties the
    //    selector-less base defaults are deferred so a parent's class-specific
    //    value can still cascade in preference to the generic default.
    const bool inheritable = is_inheritable_property(property_name);
    const auto* value = resolve_widget_rule(
        *theme, impl_->style_classes, impl_->state, property_name, inheritable ? 1 : 0);

    // 2. Inheritable properties cascade from parent to child, then fall back
    //    to this widget's base-rule default.
    if (value == nullptr && inheritable) {
        for (const Widget* p = parent(); p != nullptr; p = p->parent()) {
            value = resolve_widget_rule(
                *theme, p->impl_->style_classes, p->impl_->state, property_name, 1);
            if (value != nullptr) {
                break;
            }
        }
        if (value == nullptr) {
            value = resolve_widget_rule(*theme, impl_->style_classes, impl_->state, property_name);
        }
    }

    // 3. Global token fallback.
    if (value == nullptr) {
        value = theme->token(property_name);
        int depth = 0;
        while (value != nullptr && std::holds_alternative<std::string>(*value) && depth < 4) {
            value = theme->token(std::get<std::string>(*value));
            ++depth;
        }
    }

    if (value != nullptr && std::holds_alternative<float>(*value)) {
        return std::get<float>(*value);
    }
    return fallback;
}

std::string Widget::theme_string(std::string_view property_name, std::string_view fallback) const {
    auto theme = Theme::active();
    if (!theme) {
        return std::string(fallback);
    }

    // String policy values must not be dereferenced blindly: "overlay" is a
    // value, not a token name. Resolve the raw rule value, then follow token
    // indirection only while the string names an existing token.
    const StyleValue* value =
        theme->resolve({}, impl_->style_classes, impl_->state, property_name, 0);
    if (value == nullptr) {
        value = theme->token(property_name);
    }
    int depth = 0;
    while (value != nullptr && std::holds_alternative<std::string>(*value) && depth < 4) {
        const auto* target = theme->token(std::get<std::string>(*value));
        if (target == nullptr) {
            break;
        }
        value = target;
        ++depth;
    }

    if (value != nullptr && std::holds_alternative<std::string>(*value)) {
        return std::get<std::string>(*value);
    }
    return std::string(fallback);
}

Window* Widget::host_window() const {
    return impl_->host_window;
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
    if (ctx.debug_annotations_enabled()) {
        ++impl_->debug_hotspot_counters.snapshot_count;
        const auto path = debug_tree_path();
        ctx.push_debug_source(debug_snapshot_label(), path);
        snapshot(ctx);
        ctx.pop_debug_source();
    } else {
        snapshot(ctx);
    }
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

void Widget::clear_queued_redraw_regions() {
    impl_->queued_redraw_regions.clear();
}

bool Widget::handle_mouse_event(const MouseEvent& /*event*/) {
    return false;
}

bool Widget::handle_key_event(const KeyEvent& /*event*/) {
    return false;
}

bool Widget::handle_text_input_event(const TextInputEvent& /*event*/) {
    return false;
}

bool Widget::handle_drag_drop_event(DragDropEvent& event) {
    switch (event.type) {
    case DragDropEventType::Enter:
        impl_->on_drag_enter.emit(event);
        break;
    case DragDropEventType::Motion:
        impl_->on_drag_motion.emit(event);
        break;
    case DragDropEventType::Leave:
        impl_->on_drag_leave.emit(event);
        break;
    case DragDropEventType::Drop:
        impl_->on_drop.emit(event);
        break;
    }
    return event.is_accepted();
}

bool Widget::hit_test(Point point) const {
    return allocation().contains(point);
}

std::vector<Rect> Widget::damage_regions() const {
    if (!impl_->queued_redraw_regions.empty()) {
        std::vector<Rect> regions;
        regions.reserve(impl_->queued_redraw_regions.size());
        for (const auto& region : impl_->queued_redraw_regions) {
            regions.push_back({
                allocation().x + region.x,
                allocation().y + region.y,
                region.width,
                region.height,
            });
        }
        return regions;
    }
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

Size Widget::measure_text_wrapped(std::string_view text,
                                  const FontDescriptor& font,
                                  float max_width) const {
    if (text.empty()) {
        return {};
    }
    note_text_measure_for_diagnostics();
    if (impl_->host_window != nullptr && impl_->host_window->text_shaper() != nullptr) {
        return impl_->host_window->text_shaper()->measure_wrapped(text, font, max_width);
    }
    const auto length = static_cast<float>(text.size());
    return {std::min(max_width, length * font.size * 0.55F), font.size * 1.35F * 2.0F};
}

void Widget::add_text_elided(SnapshotContext& ctx,
                             Point origin,
                             std::string_view text,
                             float max_width,
                             Color color,
                             const FontDescriptor& font) const {
    if (text.empty() || max_width <= 0.0F) {
        return;
    }
    // No glyph advances more than one em per byte, so short runs are drawn
    // without measuring at all.
    if (static_cast<float>(text.size()) * font.size <= max_width) {
        ctx.add_text(origin, std::string(text), color, font);
        return;
    }
    add_text_elided(ctx, origin, text, measure_text(text, font), max_width, color, font);
}

void Widget::add_text_elided(SnapshotContext& ctx,
                             Point origin,
                             std::string_view text,
                             Size measured,
                             float max_width,
                             Color color,
                             const FontDescriptor& font) const {
    if (text.empty() || max_width <= 0.0F) {
        return;
    }
    if (measured.width <= max_width) {
        ctx.add_text(origin, std::string(text), color, font);
        return;
    }

    static constexpr std::string_view kEllipsis = "…";

    // Byte offsets where UTF-8 code points start, so truncation never splits
    // a multi-byte sequence.
    std::vector<std::size_t> starts;
    starts.reserve(text.size() + 1);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0U) != 0x80U) {
            starts.push_back(i);
        }
    }
    starts.push_back(text.size());

    const auto candidate_for = [&](std::size_t kept_chars) {
        auto prefix = text.substr(0, starts[kept_chars]);
        while (!prefix.empty() && prefix.back() == ' ') {
            prefix.remove_suffix(1);
        }
        std::string candidate(prefix);
        candidate += kEllipsis;
        return candidate;
    };

    // Binary search for the longest prefix whose "prefix…" still fits.
    std::size_t low = 0;
    std::size_t high = starts.size() - 1;
    while (low < high) {
        const std::size_t mid = (low + high + 1) / 2;
        if (measure_text(candidate_for(mid), font).width <= max_width) {
            low = mid;
        } else {
            high = mid - 1;
        }
    }

    const auto elided = candidate_for(low);
    if (low == 0 && measure_text(elided, font).width > max_width) {
        return;
    }
    ctx.add_text(origin, elided, color, font);
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
