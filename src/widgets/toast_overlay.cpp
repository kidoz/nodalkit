#include <algorithm>
#include <chrono>
#include <cstdint>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
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

    bool handle_mouse_event(const MouseEvent& event) override {
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
        ensure_accessible().set_hidden(true);
    }

    Toast toast_;
    mutable Rect action_bounds_{};
    Signal<> action_;
    Signal<> dismissed_;
    bool armed_ = false;
};

} // namespace

struct ToastOverlay::Impl {
    std::shared_ptr<Widget> child;
    std::shared_ptr<ToastSurface> surface;
    std::deque<Toast> toasts;
    ScopedConnection action_connection;
    ScopedConnection dismissed_connection;
    Signal<> action;
    Signal<> dismissed;
    std::uint64_t current_generation = 0;
    EventLoop* timeout_loop = nullptr;
    CallbackHandle timeout_handle;
};

std::shared_ptr<ToastOverlay> ToastOverlay::create() {
    return std::shared_ptr<ToastOverlay>(new ToastOverlay());
}

ToastOverlay::ToastOverlay() : impl_(std::make_unique<Impl>()) {
    impl_->surface = ToastSurface::create();
    impl_->surface->set_visible(false);
    append_child(impl_->surface);
    impl_->action_connection = ScopedConnection(impl_->surface->on_action().connect([this] {
        const auto generation = impl_->current_generation;
        impl_->action.emit();
        if (impl_->current_generation == generation) {
            dismiss_current();
        }
    }));
    impl_->dismissed_connection =
        ScopedConnection(impl_->surface->on_dismissed().connect([this] { dismiss_current(); }));
    add_style_class("toast-overlay");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Status);
    accessible.set_hidden(true);
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
    auto& accessible = ensure_accessible();
    accessible.set_hidden(false);
    accessible.set_name(impl_->toasts.front().title);
    if (impl_->toasts.front().action_label.empty()) {
        accessible.remove_action(AccessibleAction::Activate);
    } else {
        accessible.add_action(AccessibleAction::Activate, [this] {
            const auto generation = impl_->current_generation;
            impl_->action.emit();
            if (impl_->current_generation == generation) {
                dismiss_current();
            }
            return true;
        });
    }
    queue_redraw();
    if (changes_current) {
        schedule_timeout();
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
    auto& accessible = ensure_accessible();
    accessible.set_hidden(impl_->toasts.empty());
    accessible.set_name(impl_->toasts.empty() ? "" : impl_->toasts.front().title);
    if (impl_->toasts.empty() || impl_->toasts.front().action_label.empty()) {
        accessible.remove_action(AccessibleAction::Activate);
    } else {
        accessible.add_action(AccessibleAction::Activate, [this] {
            const auto generation = impl_->current_generation;
            impl_->action.emit();
            if (impl_->current_generation == generation) {
                dismiss_current();
            }
            return true;
        });
    }
    queue_redraw();
    impl_->dismissed.emit();
    if (impl_->toasts.empty()) {
        cancel_timeout();
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
    auto& accessible = ensure_accessible();
    accessible.set_hidden(true);
    accessible.set_name("");
    accessible.remove_action(AccessibleAction::Activate);
    queue_redraw();
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
    cancel_timeout();
    if (impl_->toasts.empty()) {
        return;
    }

    auto* loop = EventLoop::current();
    if (loop == nullptr) {
        return;
    }

    const auto generation = impl_->current_generation;
    const std::weak_ptr<Widget> weak_self = weak_from_this();
    impl_->timeout_loop = loop;
    impl_->timeout_handle = loop->set_timeout(
        std::max(std::chrono::milliseconds::zero(), impl_->toasts.front().timeout),
        [weak_self, generation] {
            const auto widget = weak_self.lock();
            const auto overlay = std::dynamic_pointer_cast<ToastOverlay>(widget);
            if (overlay == nullptr || overlay->impl_->current_generation != generation) {
                return;
            }
            overlay->impl_->timeout_loop = nullptr;
            overlay->impl_->timeout_handle = {};
            overlay->dismiss_current();
        },
        "toast-overlay-timeout");
}

std::size_t ToastOverlay::toast_count() const {
    return impl_->toasts.size();
}

const Toast* ToastOverlay::current_toast() const {
    return impl_->toasts.empty() ? nullptr : &impl_->toasts.front();
}

Signal<>& ToastOverlay::on_action() {
    return impl_->action;
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
