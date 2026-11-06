#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/runtime/event_loop.h>
#include <nk/text/font.h>
#include <nk/widgets/toast_overlay.h>
#include <utility>

namespace nk {

namespace {

class ToastSurface final : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ToastSurface> create() {
        return std::shared_ptr<ToastSurface>(new ToastSurface());
    }

    void set_toast(Toast toast) {
        toast_ = std::move(toast);
        queue_redraw();
    }

    Signal<>& on_action() { return action_; }

    Signal<>& on_dismissed() { return dismissed_; }

    void set_pointer_handler(std::function<void(bool)> handler) {
        pointer_handler_ = std::move(handler);
    }

    bool handle_mouse_event(const MouseEvent& event) override {
        const bool inside = allocation().contains({event.x, event.y});
        if (event.type == MouseEvent::Type::Enter ||
            (event.type == MouseEvent::Type::Move && inside)) {
            if (pointer_handler_) {
                pointer_handler_(true);
            }
        } else if (event.type == MouseEvent::Type::Leave ||
                   (event.type == MouseEvent::Type::Move && !inside)) {
            if (pointer_handler_) {
                pointer_handler_(false);
            }
        }
        if (event.button != 1) {
            return false;
        }
        if (event.type == MouseEvent::Type::Press) {
            armed_ = allocation().contains({event.x, event.y});
            return armed_;
        }
        if (event.type == MouseEvent::Type::Release) {
            const bool activate = armed_ && allocation().contains({event.x, event.y});
            armed_ = false;
            if (!activate) {
                return false;
            }
            if (!toast_.action_label.empty() && action_bounds_.contains({event.x, event.y})) {
                action_.emit();
            } else {
                dismissed_.emit();
            }
            return true;
        }
        return false;
    }

protected:
    void snapshot(SnapshotContext& ctx) const override {
        const auto bounds = allocation();
        const auto font = FontDescriptor{
            .family = {}, .size = theme_number("font-size", 13.0F), .weight = FontWeight::Medium};
        ctx.push_overlay_container(bounds);
        ctx.add_rounded_rect(
            bounds,
            theme_color("background",
                        theme_color("surface-osd", Color{0.15F, 0.15F, 0.17F, 0.95F})),
            theme_number("corner-radius", 12.0F));
        const auto title = measure_text(toast_.title, font);
        const auto action = measure_text(toast_.action_label, font);
        const float action_space = toast_.action_label.empty() ? 0.0F : action.width + 32.0F;
        add_text_elided(
            ctx,
            {bounds.x + 16.0F, bounds.y + ((bounds.height - title.height) * 0.5F)},
            toast_.title,
            std::max(0.0F, bounds.width - action_space - 32.0F),
            theme_color("text-color", theme_color("text-osd", Color::from_rgb(255, 255, 255))),
            font);
        if (!toast_.action_label.empty()) {
            action_bounds_ = {bounds.right() - action.width - 16.0F,
                              bounds.y,
                              action.width + 16.0F,
                              bounds.height};
            ctx.add_text(
                {action_bounds_.x, bounds.y + ((bounds.height - action.height) * 0.5F)},
                toast_.action_label,
                theme_color("action-color", theme_color("accent", Color::from_rgb(53, 132, 228))),
                font);
        } else {
            action_bounds_ = {};
        }
        ctx.pop_container();
    }

private:
    ToastSurface() {
        add_style_class("toast-surface");
        ensure_accessible().set_role(AccessibleRole::Status);
    }

    Toast toast_;
    mutable Rect action_bounds_{};
    Signal<> action_;
    Signal<> dismissed_;
    bool armed_ = false;
    std::function<void(bool)> pointer_handler_;
};

} // namespace

struct ToastOverlay::Impl {
    std::shared_ptr<Widget> child;
    std::shared_ptr<ToastSurface> surface;
    std::deque<Toast> toasts;
    ScopedConnection action_connection;
    ScopedConnection dismissed_connection;
    Signal<std::string_view> action;
    Signal<> dismissed;
    std::uint64_t current_generation = 0;
    EventLoop* timeout_loop = nullptr;
    CallbackHandle timeout_handle;
    bool pointer_inside = false;
    bool timeout_paused = false;
    std::chrono::milliseconds remaining{0};
    std::chrono::steady_clock::time_point deadline{};
};

std::shared_ptr<ToastOverlay> ToastOverlay::create() {
    return std::shared_ptr<ToastOverlay>(new ToastOverlay());
}

ToastOverlay::ToastOverlay() : impl_(std::make_unique<Impl>()) {
    impl_->surface = ToastSurface::create();
    impl_->surface->set_visible(false);
    append_child(impl_->surface);
    impl_->surface->set_pointer_handler([this](bool inside) { set_pointer_inside(inside); });
    impl_->action_connection = ScopedConnection(impl_->surface->on_action().connect([this] {
        const auto generation = impl_->current_generation;
        emit_current_action();
        if (impl_->current_generation == generation) {
            dismiss_current();
        }
    }));
    impl_->dismissed_connection =
        ScopedConnection(impl_->surface->on_dismissed().connect([this] { dismiss_current(); }));
    add_style_class("toast-overlay");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Group);
    accessible.set_name("Content and notifications");
}

ToastOverlay::~ToastOverlay() {
    cancel_timeout();
}

void ToastOverlay::set_child(std::shared_ptr<Widget> child) {
    if (impl_->child == child) {
        return;
    }
    if (impl_->child != nullptr) {
        remove_child(*impl_->child);
    }
    impl_->child = std::move(child);
    if (impl_->child != nullptr) {
        insert_child(0, impl_->child);
    }
    queue_layout();
}

Widget* ToastOverlay::child() const {
    return impl_->child.get();
}

void ToastOverlay::add_toast(Toast toast) {
    if (toast.title.empty()) {
        return;
    }
    const bool changes_current = impl_->toasts.empty() || toast.priority == ToastPriority::High;
    if (toast.priority == ToastPriority::High) {
        impl_->toasts.push_front(std::move(toast));
    } else {
        impl_->toasts.push_back(std::move(toast));
    }
    if (changes_current) {
        ++impl_->current_generation;
    }
    impl_->surface->set_toast(impl_->toasts.front());
    impl_->surface->set_visible(true);
    auto& accessible = impl_->surface->ensure_accessible();
    accessible.set_hidden(false);
    accessible.set_name(impl_->toasts.front().title);
    if (impl_->toasts.front().action_label.empty()) {
        accessible.remove_action(AccessibleAction::Activate);
    } else {
        accessible.add_action(AccessibleAction::Activate, [this] {
            const auto generation = impl_->current_generation;
            emit_current_action();
            if (impl_->current_generation == generation) {
                dismiss_current();
            }
            return true;
        });
    }
    queue_redraw();
    if (changes_current) {
        if (impl_->pointer_inside) {
            cancel_timeout();
            impl_->remaining = impl_->toasts.front().timeout;
            impl_->timeout_paused = true;
        } else {
            schedule_timeout();
        }
    }
}

void ToastOverlay::dismiss_current() {
    if (impl_->toasts.empty()) {
        return;
    }
    impl_->toasts.pop_front();
    ++impl_->current_generation;
    impl_->surface->set_visible(!impl_->toasts.empty());
    if (!impl_->toasts.empty()) {
        impl_->surface->set_toast(impl_->toasts.front());
    }
    auto& accessible = impl_->surface->ensure_accessible();
    accessible.set_hidden(impl_->toasts.empty());
    accessible.set_name(impl_->toasts.empty() ? "" : impl_->toasts.front().title);
    if (impl_->toasts.empty() || impl_->toasts.front().action_label.empty()) {
        accessible.remove_action(AccessibleAction::Activate);
    } else {
        accessible.add_action(AccessibleAction::Activate, [this] {
            const auto generation = impl_->current_generation;
            emit_current_action();
            if (impl_->current_generation == generation) {
                dismiss_current();
            }
            return true;
        });
    }
    queue_redraw();
    impl_->dismissed.emit();
    if (impl_->toasts.empty()) {
        impl_->timeout_paused = false;
        cancel_timeout();
    } else if (impl_->pointer_inside) {
        cancel_timeout();
        impl_->remaining = impl_->toasts.front().timeout;
        impl_->timeout_paused = true;
    } else {
        schedule_timeout();
    }
}

void ToastOverlay::dismiss_all() {
    if (impl_->toasts.empty()) {
        return;
    }
    impl_->toasts.clear();
    ++impl_->current_generation;
    impl_->surface->set_visible(false);
    auto& accessible = impl_->surface->ensure_accessible();
    accessible.set_hidden(true);
    accessible.set_name("");
    accessible.remove_action(AccessibleAction::Activate);
    queue_redraw();
    impl_->timeout_paused = false;
    cancel_timeout();
    impl_->dismissed.emit();
}

void ToastOverlay::cancel_timeout() {
    if (impl_->timeout_handle.valid() && EventLoop::current() == impl_->timeout_loop) {
        impl_->timeout_loop->cancel(impl_->timeout_handle);
    }
    impl_->timeout_loop = nullptr;
    impl_->timeout_handle = {};
}

void ToastOverlay::schedule_timeout() {
    if (impl_->toasts.empty()) {
        cancel_timeout();
        return;
    }
    const auto duration =
        std::max(std::chrono::milliseconds::zero(), impl_->toasts.front().timeout);
    cancel_timeout();
    impl_->timeout_paused = false;
    impl_->remaining = duration;
    auto* loop = EventLoop::current();
    if (loop == nullptr) {
        return;
    }

    const auto generation = impl_->current_generation;
    const std::weak_ptr<Widget> weak_self = weak_from_this();
    impl_->deadline = std::chrono::steady_clock::now() + duration;
    impl_->timeout_loop = loop;
    impl_->timeout_handle = loop->set_timeout(
        duration,
        [weak_self, generation] {
            const auto widget = weak_self.lock();
            const auto overlay = std::dynamic_pointer_cast<ToastOverlay>(widget);
            if (overlay == nullptr || overlay->impl_->current_generation != generation ||
                overlay->impl_->timeout_paused) {
                return;
            }
            overlay->impl_->timeout_loop = nullptr;
            overlay->impl_->timeout_handle = {};
            overlay->dismiss_current();
        },
        "toast-overlay-timeout");
}

void ToastOverlay::pause_timeout() {
    if (impl_->timeout_paused || impl_->toasts.empty()) {
        return;
    }
    if (impl_->timeout_handle.valid()) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            impl_->deadline - std::chrono::steady_clock::now());
        impl_->remaining = std::max(std::chrono::milliseconds::zero(), left);
    }
    impl_->timeout_paused = true;
    cancel_timeout();
}

void ToastOverlay::resume_timeout() {
    if (!impl_->timeout_paused || impl_->toasts.empty()) {
        return;
    }
    const auto duration = impl_->remaining;
    impl_->timeout_paused = false;
    if (duration.count() < 0) {
        dismiss_current();
        return;
    }
    // schedule_timeout reads the toast timeout. Temporarily substitute the
    // remaining time so a hover does not restart the full interval.
    auto& timeout = impl_->toasts.front().timeout;
    const auto saved = timeout;
    timeout = duration;
    schedule_timeout();
    if (!impl_->toasts.empty()) {
        impl_->toasts.front().timeout = saved;
    }
}

void ToastOverlay::set_pointer_inside(bool inside) {
    if (impl_->pointer_inside == inside) {
        return;
    }
    impl_->pointer_inside = inside;
    if (impl_->toasts.empty()) {
        return;
    }
    if (inside) {
        pause_timeout();
    } else {
        resume_timeout();
    }
}

void ToastOverlay::emit_current_action() {
    if (impl_->toasts.empty()) {
        return;
    }
    const std::string title = impl_->toasts.front().title;
    impl_->action.emit(title);
}

std::size_t ToastOverlay::toast_count() const {
    return impl_->toasts.size();
}

const Toast* ToastOverlay::current_toast() const {
    return impl_->toasts.empty() ? nullptr : &impl_->toasts.front();
}

Signal<std::string_view>& ToastOverlay::on_action() {
    return impl_->action;
}

bool ToastOverlay::handle_key_event(const KeyEvent& event) {
    if (impl_->toasts.empty() || event.type != KeyEvent::Type::Press) {
        return false;
    }
    if (event.key == KeyCode::Escape) {
        dismiss_current();
        return true;
    }
    if (impl_->toasts.front().action_label.empty()) {
        return false;
    }
    if (event.key == KeyCode::Return || event.key == KeyCode::Space) {
        const auto generation = impl_->current_generation;
        emit_current_action();
        if (impl_->current_generation == generation) {
            dismiss_current();
        }
        return true;
    }
    return false;
}

Signal<>& ToastOverlay::on_dismissed() {
    return impl_->dismissed;
}

SizeRequest ToastOverlay::measure(const Constraints& constraints) const {
    return impl_->child != nullptr ? impl_->child->measure_for_diagnostics(constraints)
                                   : SizeRequest{};
}

void ToastOverlay::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    if (impl_->child != nullptr) {
        impl_->child->allocate(allocation);
    }
    const float width = std::min(480.0F, std::max(0.0F, allocation.width - 32.0F));
    impl_->surface->allocate({allocation.x + ((allocation.width - width) * 0.5F),
                              allocation.bottom() - 64.0F,
                              width,
                              48.0F});
}

bool ToastOverlay::handle_mouse_event(const MouseEvent& /*event*/) {
    return false;
}

void ToastOverlay::snapshot(SnapshotContext& ctx) const {
    Widget::snapshot(ctx);
}

} // namespace nk
