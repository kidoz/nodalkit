#include <nk/platform/window.h>

#include <nk/controllers/event_controller.h>
#include <nk/foundation/logging.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/platform_backend.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/text_shaper.h>
#include <nk/ui_core/widget.h>

namespace nk {

struct Window::Impl {
    WindowConfig config;
    std::shared_ptr<Widget> child;
    std::unique_ptr<NativeSurface> surface;
    SoftwareRenderer renderer;
    std::unique_ptr<TextShaper> text_shaper;
    Widget* hovered_widget = nullptr;
    Widget* focused_widget = nullptr;
    Widget* pressed_widget = nullptr;

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

    auto const children = widget->children();
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

} // namespace

Window::Window(WindowConfig config)
    : impl_(std::make_unique<Impl>()) {
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
    return {static_cast<float>(impl_->config.width),
            static_cast<float>(impl_->config.height)};
}

void Window::set_child(std::shared_ptr<Widget> child) {
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
        if (app) {
            impl_->surface =
                app->platform_backend().create_surface(impl_->config, *this);
        }
    }

    impl_->visible = true;

    if (impl_->surface) {
        impl_->surface->show();
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

        auto const sz = size();

        // 1. Layout pass.
        if (impl_->needs_layout) {
            auto const constraints = Constraints::tight(sz);
            (void)impl_->child->measure(constraints);
            impl_->child->allocate({0, 0, sz.width, sz.height});
            impl_->needs_layout = false;
        }

        // 2. Snapshot pass.
        SnapshotContext snap_ctx;
        impl_->child->snapshot(snap_ctx);
        auto root_node = snap_ctx.take_root();

        // 3. Render pass.
        impl_->renderer.begin_frame(sz);
        if (root_node) {
            impl_->renderer.render(*root_node);
        }
        impl_->renderer.end_frame();

        // 4. Present pass.
        if (impl_->surface) {
            impl_->surface->present(
                impl_->renderer.pixel_data(),
                impl_->renderer.pixel_width(),
                impl_->renderer.pixel_height());
        }
    });
}

void Window::invalidate_layout() {
    impl_->needs_layout = true;
    request_frame();
}

// --- Event dispatch ---

void Window::dispatch_mouse_event(MouseEvent const& event) {
    if (!impl_->child) {
        return;
    }

    auto const point = Point{event.x, event.y};
    auto resolve_target = [this, point]() -> Widget* {
        if (impl_->focused_widget != nullptr
            && impl_->focused_widget->hit_test(point)) {
            return impl_->focused_widget;
        }
        return hit_test_widget(impl_->child.get(), point);
    };
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
    auto set_focused_widget = [this](Widget* widget) {
        if (impl_->focused_widget == widget) {
            return;
        }

        if (impl_->focused_widget != nullptr) {
            impl_->focused_widget->set_state_flag(StateFlags::Focused, false);
            impl_->focused_widget->on_focus_changed(false);
        }
        impl_->focused_widget = widget;
        if (impl_->focused_widget != nullptr) {
            impl_->focused_widget->set_state_flag(StateFlags::Focused, true);
            impl_->focused_widget->on_focus_changed(true);
        }
    };

    switch (event.type) {
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Move: {
        auto* target = resolve_target();
        set_hovered_widget(target);
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(
                StateFlags::Pressed, impl_->pressed_widget == target);
        }
        if (target != nullptr) {
            (void)target->handle_mouse_event(event);
        }
        break;
    }
    case MouseEvent::Type::Leave:
        set_hovered_widget(nullptr);
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
        }
        break;
    case MouseEvent::Type::Press: {
        auto* target = resolve_target();
        set_hovered_widget(target);
        impl_->pressed_widget = target;
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed, true);
        }
        set_focused_widget(
            (target != nullptr && target->is_focusable()) ? target : nullptr);
        if (target != nullptr) {
            (void)target->handle_mouse_event(event);
        }
        break;
    }
    case MouseEvent::Type::Release: {
        auto* target = resolve_target();
        set_hovered_widget(target);
        if (impl_->pressed_widget != nullptr) {
            auto* pressed = impl_->pressed_widget;
            (void)pressed->handle_mouse_event(event);
            pressed->set_state_flag(StateFlags::Pressed, false);
            impl_->pressed_widget = nullptr;
        } else if (target != nullptr) {
            (void)target->handle_mouse_event(event);
        }
        break;
    }
    case MouseEvent::Type::Scroll: {
        auto* target = resolve_target();
        set_hovered_widget(target);
        if (target != nullptr) {
            (void)target->handle_mouse_event(event);
        }
        break;
    }
    }
}

void Window::dispatch_key_event(KeyEvent const& event) {
    if (impl_->focused_widget != nullptr) {
        (void)impl_->focused_widget->handle_key_event(event);
    }
}

void Window::dispatch_window_event(WindowEvent const& event) {
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
    auto set_focused_widget = [this](Widget* widget) {
        if (impl_->focused_widget == widget) {
            return;
        }

        if (impl_->focused_widget != nullptr) {
            impl_->focused_widget->set_state_flag(StateFlags::Focused, false);
            impl_->focused_widget->on_focus_changed(false);
        }
        impl_->focused_widget = widget;
        if (impl_->focused_widget != nullptr) {
            impl_->focused_widget->set_state_flag(StateFlags::Focused, true);
            impl_->focused_widget->on_focus_changed(true);
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
        impl_->close_request.emit();
        break;
    case WindowEvent::Type::Expose:
        request_frame();
        break;
    case WindowEvent::Type::FocusIn:
        break;
    case WindowEvent::Type::FocusOut:
        set_hovered_widget(nullptr);
        if (impl_->pressed_widget != nullptr) {
            impl_->pressed_widget->set_state_flag(StateFlags::Pressed, false);
            impl_->pressed_widget = nullptr;
        }
        set_focused_widget(nullptr);
        break;
    }
}

NativeSurface* Window::native_surface() const {
    return impl_->surface.get();
}

TextShaper* Window::text_shaper() const {
    return impl_->text_shaper.get();
}

Signal<>& Window::on_close_request() {
    return impl_->close_request;
}

Signal<int, int>& Window::on_resize() {
    return impl_->resize_signal;
}

} // namespace nk
