#include <algorithm>
#include <array>
#include <nk/controllers/event_controller.h>
#include <nk/foundation/logging.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/platform_backend.h>
#include <nk/platform/window.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/text_shaper.h>
#include <nk/ui_core/widget.h>
#include <vector>

namespace nk {

struct Window::Impl {
    struct OverlayEntry {
        std::shared_ptr<Widget> widget;
        bool modal = false;
        std::weak_ptr<Widget> previous_focus;
    };

    WindowConfig config;
    std::shared_ptr<Widget> child;
    std::vector<OverlayEntry> overlays;
    std::unique_ptr<NativeSurface> surface;
    SoftwareRenderer renderer;
    std::unique_ptr<TextShaper> text_shaper;
    Widget* hovered_widget = nullptr;
    Widget* focused_widget = nullptr;
    Widget* pressed_widget = nullptr;
    CursorShape current_cursor_shape = CursorShape::Default;
    std::weak_ptr<Widget> pending_focus_restore;
    std::array<bool, 256> key_state{};

    bool visible = false;
    bool needs_layout = true;
    bool frame_pending = false;

    Signal<> close_request;
    Signal<int, int> resize_signal;
};

namespace {

Widget* hit_test_widget(Widget* widget, Point point) {
    if (widget == nullptr || !widget->is_visible() || !widget->hit_test(point)) {
        return nullptr;
    }

    const auto children = widget->children();
    for (auto it = children.rbegin(); it != children.rend(); ++it) {
        if (*it == nullptr) {
            continue;
        }
        if (auto* hit = hit_test_widget(it->get(), point)) {
            return hit;
        }
    }

    return widget;
}

bool is_descendant_of(const Widget* widget, const Widget* ancestor) {
    auto* current = widget;
    while (current != nullptr) {
        if (current == ancestor) {
            return true;
        }
        current = current->parent();
    }
    return false;
}

bool is_attached_to_window_root(const Widget* widget, const std::shared_ptr<Widget>& root) {
    return widget != nullptr && root != nullptr && is_descendant_of(widget, root.get());
}

std::vector<Widget*> widget_path_to_root(Widget* widget) {
    std::vector<Widget*> path;
    for (auto* current = widget; current != nullptr; current = current->parent()) {
        path.push_back(current);
    }
    return path;
}

void collect_focusable_widgets(Widget* widget, std::vector<Widget*>& out) {
    if (widget == nullptr || !widget->is_visible() || !widget->is_sensitive()) {
        return;
    }

    if (widget->is_focusable()) {
        out.push_back(widget);
    }

    for (const auto& child : widget->children()) {
        if (child != nullptr) {
            collect_focusable_widgets(child.get(), out);
        }
    }
}

bool can_focus_widget(const Widget* widget) {
    return widget != nullptr && widget->is_focusable() && widget->is_visible() &&
           widget->is_sensitive();
}

} // namespace

Window::Window(WindowConfig config) : impl_(std::make_unique<Impl>()) {
    impl_->config = std::move(config);
    impl_->text_shaper = TextShaper::create();
    if (impl_->text_shaper) {
        impl_->renderer.set_text_shaper(impl_->text_shaper.get());
    }
    NK_LOG_DEBUG("Window", "Window created");
}

Window::~Window() {
    NK_LOG_DEBUG("Window", "Window destroyed");
}

void Window::set_title(std::string_view title) {
    impl_->config.title = std::string(title);
    if (impl_->surface) {
        impl_->surface->set_title(title);
    }
}

std::string_view Window::title() const {
    return impl_->config.title;
}

void Window::resize(int width, int height) {
    impl_->config.width = width;
    impl_->config.height = height;
    if (impl_->surface) {
        impl_->surface->resize(width, height);
    }
    impl_->needs_layout = true;
    impl_->resize_signal.emit(width, height);
    request_frame();
}

Size Window::size() const {
    if (impl_->surface) {
        return impl_->surface->size();
    }
    return {static_cast<float>(impl_->config.width), static_cast<float>(impl_->config.height)};
}

void Window::set_child(std::shared_ptr<Widget> child) {
    impl_->hovered_widget = nullptr;
    impl_->pressed_widget = nullptr;
    impl_->focused_widget = nullptr;
    impl_->pending_focus_restore.reset();
    impl_->current_cursor_shape = CursorShape::Default;
    if (impl_->child) {
        impl_->child->set_host_window(nullptr);
    }
    impl_->child = std::move(child);
    if (impl_->child) {
        impl_->child->set_host_window(this);
    }
    impl_->needs_layout = true;
    request_frame();
}

Widget* Window::child() const {
    return impl_->child.get();
}

void Window::present() {
    // Create native surface if we have a platform backend.
    if (!impl_->surface) {
        auto* app = Application::instance();
        if (app && app->has_platform_backend()) {
            impl_->surface = app->platform_backend().create_surface(impl_->config, *this);
        }
    }

    impl_->visible = true;

    if (impl_->surface) {
        impl_->surface->show();
        impl_->surface->set_cursor_shape(impl_->current_cursor_shape);
    }

    impl_->needs_layout = true;
    request_frame();

    NK_LOG_INFO("Window", "Window presented");
}

void Window::hide() {
    impl_->visible = false;
    if (impl_->surface) {
        impl_->surface->hide();
    }
}

void Window::close() {
    impl_->close_request.emit();
    hide();
}

bool Window::is_visible() const {
    return impl_->visible;
}

void Window::set_fullscreen(bool fullscreen) {
    if (impl_->surface) {
        impl_->surface->set_fullscreen(fullscreen);
    }
}

bool Window::is_fullscreen() const {
    if (impl_->surface) {
        return impl_->surface->is_fullscreen();
    }
    return false;
}

void Window::request_frame() {
    if (impl_->frame_pending || !impl_->visible) {
        return;
    }
    impl_->frame_pending = true;

    auto* app = Application::instance();
    if (!app) {
        return;
    }

    (void)app->event_loop().add_idle([this] {
        impl_->frame_pending = false;

        if (!impl_->visible || !impl_->child) {
            return;
        }

        const auto sz = size();
        const float scale_factor =
            impl_->surface != nullptr ? impl_->surface->scale_factor() : 1.0F;

        // 1. Layout pass.
        if (impl_->needs_layout) {
            const auto constraints = Constraints::tight(sz);
            (void)impl_->child->measure(constraints);
            impl_->child->allocate({0, 0, sz.width, sz.height});
            for (auto& overlay : impl_->overlays) {
                if (overlay.widget != nullptr && overlay.widget->is_visible()) {
                    overlay.widget->allocate({0, 0, sz.width, sz.height});
                }
            }
            impl_->needs_layout = false;
        }

        // 2. Snapshot pass.
        SnapshotContext snap_ctx;
        impl_->child->snapshot(snap_ctx);
        for (auto& overlay : impl_->overlays) {
            if (overlay.widget != nullptr && overlay.widget->is_visible()) {
                overlay.widget->snapshot(snap_ctx);
            }
        }
        auto root_node = snap_ctx.take_root();

        // 3. Render pass.
        impl_->renderer.begin_frame(sz, scale_factor);
        if (root_node) {
            impl_->renderer.render(*root_node);
        }
        impl_->renderer.end_frame();

        // 4. Present pass.
        if (impl_->surface) {
            impl_->surface->present(impl_->renderer.pixel_data(),
                                    impl_->renderer.pixel_width(),
                                    impl_->renderer.pixel_height());
        }
    });
}

void Window::invalidate_layout() {
    impl_->needs_layout = true;
    request_frame();
}

void Window::focus_widget(Widget* widget) {
    auto is_attached_to_window = [this](const Widget* candidate) {
        if (candidate == nullptr) {
            return false;
        }
        if (is_attached_to_window_root(candidate, impl_->child)) {
            return true;
        }
        for (const auto& overlay : impl_->overlays) {
            if (overlay.widget != nullptr && is_descendant_of(candidate, overlay.widget.get())) {
                return true;
            }
        }
        return false;
    };

    if (widget != nullptr && (!can_focus_widget(widget) || !is_attached_to_window(widget))) {
        widget = nullptr;
    }

    if (impl_->focused_widget == widget) {
        return;
    }

    if (impl_->focused_widget != nullptr) {
        impl_->focused_widget->set_state_flag(StateFlags::Focused, false);
        impl_->focused_widget->dispatch_focus_controllers(false);
        impl_->focused_widget->on_focus_changed(false);
    }

    impl_->focused_widget = widget;

    if (impl_->focused_widget != nullptr) {
        impl_->focused_widget->set_state_flag(StateFlags::Focused, true);
        impl_->focused_widget->dispatch_focus_controllers(true);
        impl_->focused_widget->on_focus_changed(true);
        impl_->pending_focus_restore = impl_->focused_widget->shared_from_this();
    }
}

void Window::handle_widget_state_change(Widget& widget) {
    if (!widget.is_visible() || !widget.is_sensitive()) {
        if (impl_->hovered_widget != nullptr && is_descendant_of(impl_->hovered_widget, &widget)) {
            impl_->hovered_widget->set_state_flag(StateFlags::Hovered, false);
            impl_->hovered_widget = nullptr;
            impl_->current_cursor_shape = CursorShape::Default;
            if (impl_->surface != nullptr) {
                impl_->surface->set_cursor_shape(CursorShape::Default);
            }
        }
        if (impl_->pressed_widget != nullptr && is_descendant_of(impl_->pressed_widget, &widget)) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
            impl_->pressed_widget = nullptr;
        }
        if (impl_->focused_widget != nullptr && is_descendant_of(impl_->focused_widget, &widget)) {
            focus_widget(nullptr);
        }
    }
}

void Window::handle_widget_detached(Widget& widget) {
    if (impl_->hovered_widget != nullptr && is_descendant_of(impl_->hovered_widget, &widget)) {
        impl_->hovered_widget->set_state_flag(StateFlags::Hovered, false);
        impl_->hovered_widget = nullptr;
        impl_->current_cursor_shape = CursorShape::Default;
        if (impl_->surface != nullptr) {
            impl_->surface->set_cursor_shape(CursorShape::Default);
        }
    }
    if (impl_->pressed_widget != nullptr && is_descendant_of(impl_->pressed_widget, &widget)) {
        impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
        impl_->pressed_widget = nullptr;
    }
    if (impl_->focused_widget != nullptr && is_descendant_of(impl_->focused_widget, &widget)) {
        focus_widget(nullptr);
    }
    if (auto pending = impl_->pending_focus_restore.lock();
        pending != nullptr && is_descendant_of(pending.get(), &widget)) {
        impl_->pending_focus_restore.reset();
    }
}

CursorShape Window::current_cursor_shape() const {
    return impl_->current_cursor_shape;
}

bool Window::is_key_pressed(KeyCode key) const {
    const auto index = static_cast<std::size_t>(key);
    if (index >= impl_->key_state.size()) {
        return false;
    }
    return impl_->key_state[index];
}

// --- Event dispatch ---

void Window::dispatch_mouse_event(const MouseEvent& event) {
    if (!impl_->child) {
        return;
    }

    const auto point = Point{event.x, event.y};
    auto resolve_overlay_target = [this, point]() -> Widget* {
        for (auto it = impl_->overlays.rbegin(); it != impl_->overlays.rend(); ++it) {
            if (it->widget == nullptr || !it->widget->is_visible()) {
                continue;
            }
            if (auto* hit = hit_test_widget(it->widget.get(), point)) {
                return hit;
            }
            if (it->modal && it->widget->hit_test(point)) {
                return it->widget.get();
            }
        }
        return nullptr;
    };
    auto resolve_target = [this, point, &resolve_overlay_target]() -> Widget* {
        if (auto* overlay_target = resolve_overlay_target()) {
            return overlay_target;
        }
        if (impl_->focused_widget != nullptr && impl_->focused_widget->hit_test(point)) {
            return impl_->focused_widget;
        }
        return hit_test_widget(impl_->child.get(), point);
    };
    auto set_hovered_widget = [this, point](Widget* widget) {
        if (impl_->hovered_widget == widget) {
            return;
        }

        const auto previous_path = widget_path_to_root(impl_->hovered_widget);
        const auto next_path = widget_path_to_root(widget);
        std::size_t shared_suffix = 0;
        while (shared_suffix < previous_path.size() && shared_suffix < next_path.size() &&
               previous_path[previous_path.size() - 1 - shared_suffix] ==
                   next_path[next_path.size() - 1 - shared_suffix]) {
            ++shared_suffix;
        }

        MouseEvent leave_event{
            .type = MouseEvent::Type::Leave,
            .x = point.x,
            .y = point.y,
        };
        for (std::size_t i = 0; i + shared_suffix < previous_path.size(); ++i) {
            previous_path[i]->dispatch_pointer_controllers(leave_event);
        }

        if (impl_->hovered_widget != nullptr) {
            impl_->hovered_widget->set_state_flag(StateFlags::Hovered, false);
        }
        impl_->hovered_widget = widget;
        if (impl_->hovered_widget != nullptr) {
            impl_->hovered_widget->set_state_flag(StateFlags::Hovered, true);
        }

        MouseEvent enter_event{
            .type = MouseEvent::Type::Enter,
            .x = point.x,
            .y = point.y,
        };
        for (std::size_t remaining = next_path.size() - shared_suffix; remaining > 0; --remaining) {
            next_path[remaining - 1]->dispatch_pointer_controllers(enter_event);
        }
    };
    auto dispatch_mouse_bubble = [&event](Widget* target) {
        for (auto* current = target; current != nullptr; current = current->parent()) {
            current->dispatch_pointer_controllers(event);
            if (current->handle_mouse_event(event)) {
                return true;
            }
        }
        return false;
    };
    auto update_cursor_shape = [this](Widget* widget) {
        const CursorShape next_shape =
            widget != nullptr ? widget->cursor_shape() : CursorShape::Default;
        if (impl_->current_cursor_shape == next_shape) {
            return;
        }
        impl_->current_cursor_shape = next_shape;
        if (impl_->surface != nullptr) {
            impl_->surface->set_cursor_shape(next_shape);
        }
    };

    switch (event.type) {
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Move: {
        auto* target = resolve_target();
        set_hovered_widget(target);
        update_cursor_shape(target);
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed,
                                                  impl_->pressed_widget == target);
        }
        if (target != nullptr) {
            (void)dispatch_mouse_bubble(target);
        }
        break;
    }
    case MouseEvent::Type::Leave:
        set_hovered_widget(nullptr);
        update_cursor_shape(nullptr);
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
        }
        break;
    case MouseEvent::Type::Press: {
        auto* target = resolve_target();
        set_hovered_widget(target);
        update_cursor_shape(target);
        impl_->pressed_widget = target;
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed, true);
        }
        focus_widget(target);
        if (target != nullptr) {
            (void)dispatch_mouse_bubble(target);
        }
        break;
    }
    case MouseEvent::Type::Release: {
        auto* target = resolve_target();
        set_hovered_widget(target);
        update_cursor_shape(target);
        if (impl_->pressed_widget != nullptr) {
            auto* pressed = impl_->pressed_widget;
            (void)dispatch_mouse_bubble(pressed);
            pressed->set_state_flag(StateFlags::Pressed, false);
            impl_->pressed_widget = nullptr;
        } else if (target != nullptr) {
            (void)dispatch_mouse_bubble(target);
        }
        break;
    }
    case MouseEvent::Type::Scroll: {
        auto* target = resolve_target();
        set_hovered_widget(target);
        update_cursor_shape(target);
        if (target != nullptr) {
            (void)dispatch_mouse_bubble(target);
        }
        break;
    }
    }
}

void Window::dispatch_key_event(const KeyEvent& event) {
    const auto key_index = static_cast<std::size_t>(event.key);
    if (key_index < impl_->key_state.size()) {
        impl_->key_state[key_index] = event.type == KeyEvent::Type::Press;
    }

    auto dispatch_key_bubble = [&event](Widget* target) {
        for (auto* current = target; current != nullptr; current = current->parent()) {
            current->dispatch_keyboard_controllers(event);
            if (current->handle_key_event(event)) {
                return true;
            }
        }
        return false;
    };

    auto advance_focus = [this](bool reverse) -> bool {
        Widget* scope = impl_->child.get();
        for (auto it = impl_->overlays.rbegin(); it != impl_->overlays.rend(); ++it) {
            if (it->modal && it->widget != nullptr && it->widget->is_visible()) {
                scope = it->widget.get();
                break;
            }
        }

        std::vector<Widget*> focusable_widgets;
        collect_focusable_widgets(scope, focusable_widgets);
        if (focusable_widgets.empty()) {
            focus_widget(nullptr);
            return false;
        }

        const auto current =
            std::find(focusable_widgets.begin(), focusable_widgets.end(), impl_->focused_widget);
        std::size_t index = 0;
        if (current == focusable_widgets.end()) {
            index = reverse ? focusable_widgets.size() - 1 : 0;
        } else if (reverse) {
            index = current == focusable_widgets.begin()
                        ? focusable_widgets.size() - 1
                        : static_cast<std::size_t>((current - focusable_widgets.begin()) - 1);
        } else {
            index = static_cast<std::size_t>((current - focusable_widgets.begin() + 1) %
                                             focusable_widgets.size());
        }

        focus_widget(focusable_widgets[index]);
        return true;
    };

    if (event.type == KeyEvent::Type::Press && event.key == KeyCode::Tab &&
        (event.modifiers == Modifiers::None || event.modifiers == Modifiers::Shift)) {
        (void)advance_focus((event.modifiers & Modifiers::Shift) == Modifiers::Shift);
        return;
    }

    for (auto it = impl_->overlays.rbegin(); it != impl_->overlays.rend(); ++it) {
        if (!it->modal || it->widget == nullptr || !it->widget->is_visible()) {
            continue;
        }

        if (impl_->focused_widget != nullptr &&
            is_descendant_of(impl_->focused_widget, it->widget.get())) {
            (void)dispatch_key_bubble(impl_->focused_widget);
        } else {
            (void)dispatch_key_bubble(it->widget.get());
        }
        return;
    }

    if (impl_->focused_widget != nullptr) {
        (void)dispatch_key_bubble(impl_->focused_widget);
    }
}

void Window::dispatch_window_event(const WindowEvent& event) {
    auto set_hovered_widget = [this](Widget* widget) {
        if (impl_->hovered_widget == widget) {
            return;
        }

        if (impl_->hovered_widget != nullptr) {
            impl_->hovered_widget->set_state_flag(StateFlags::Hovered, false);
        }
        impl_->hovered_widget = widget;
        if (impl_->hovered_widget != nullptr) {
            impl_->hovered_widget->set_state_flag(StateFlags::Hovered, true);
        }
    };

    switch (event.type) {
    case WindowEvent::Type::Resize:
        impl_->config.width = event.width;
        impl_->config.height = event.height;
        impl_->needs_layout = true;
        impl_->resize_signal.emit(event.width, event.height);
        request_frame();
        break;
    case WindowEvent::Type::Close:
        close();
        break;
    case WindowEvent::Type::Expose:
        request_frame();
        break;
    case WindowEvent::Type::FocusIn:
        if (auto restore = impl_->pending_focus_restore.lock()) {
            focus_widget(restore.get());
        }
        break;
    case WindowEvent::Type::FocusOut:
        set_hovered_widget(nullptr);
        impl_->current_cursor_shape = CursorShape::Default;
        if (impl_->surface != nullptr) {
            impl_->surface->set_cursor_shape(CursorShape::Default);
        }
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
            impl_->pressed_widget = nullptr;
        }
        if (impl_->focused_widget != nullptr) {
            impl_->pending_focus_restore = impl_->focused_widget->shared_from_this();
        }
        impl_->key_state.fill(false);
        focus_widget(nullptr);
        break;
    }
}

NativeSurface* Window::native_surface() const {
    return impl_->surface.get();
}

TextShaper* Window::text_shaper() const {
    return impl_->text_shaper.get();
}

void Window::show_overlay(std::shared_ptr<Widget> overlay, bool modal) {
    if (overlay == nullptr) {
        return;
    }

    for (auto& entry : impl_->overlays) {
        if (entry.widget.get() == overlay.get()) {
            entry.widget = std::move(overlay);
            entry.modal = modal;
            entry.previous_focus = modal && impl_->focused_widget != nullptr
                                       ? impl_->focused_widget->shared_from_this()
                                       : std::weak_ptr<Widget>{};
            entry.widget->set_host_window(this);
            impl_->needs_layout = true;
            request_frame();
            return;
        }
    }

    overlay->set_host_window(this);
    impl_->overlays.push_back({std::move(overlay),
                               modal,
                               modal && impl_->focused_widget != nullptr
                                   ? impl_->focused_widget->shared_from_this()
                                   : std::weak_ptr<Widget>{}});
    impl_->needs_layout = true;
    request_frame();
}

void Window::dismiss_overlay(Widget& overlay) {
    auto it = std::find_if(
        impl_->overlays.begin(), impl_->overlays.end(), [&](const Impl::OverlayEntry& entry) {
            return entry.widget.get() == &overlay;
        });
    if (it == impl_->overlays.end()) {
        return;
    }

    if (impl_->hovered_widget != nullptr && is_descendant_of(impl_->hovered_widget, &overlay)) {
        impl_->hovered_widget->set_state_flag(StateFlags::Hovered, false);
        impl_->hovered_widget = nullptr;
    }
    if (impl_->pressed_widget != nullptr && is_descendant_of(impl_->pressed_widget, &overlay)) {
        impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
        impl_->pressed_widget = nullptr;
    }
    if (impl_->focused_widget != nullptr && is_descendant_of(impl_->focused_widget, &overlay)) {
        focus_widget(nullptr);
    }

    auto restore_focus = it->previous_focus;

    it->widget->set_host_window(nullptr);
    impl_->overlays.erase(it);
    impl_->needs_layout = true;
    request_frame();

    if (auto widget = restore_focus.lock()) {
        focus_widget(widget.get());
    }
}

Signal<>& Window::on_close_request() {
    return impl_->close_request;
}

Signal<int, int>& Window::on_resize() {
    return impl_->resize_signal;
}

} // namespace nk
