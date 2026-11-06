#pragma once

/// @file toast_overlay.h
/// @brief Queued transient feedback above page content.

#include <chrono>
#include <cstddef>
#include <deque>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

enum class ToastPriority {
    Normal,
    High,
};

struct Toast {
    std::string title;
    std::string action_label;
    ToastPriority priority = ToastPriority::Normal;
    std::chrono::milliseconds timeout{5000};
};

class ToastOverlay : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ToastOverlay> create();
    ~ToastOverlay() override;

    void set_child(std::shared_ptr<Widget> child);
    [[nodiscard]] Widget* child() const;
    void add_toast(Toast toast);
    void dismiss_current();
    void dismiss_all();
    [[nodiscard]] std::size_t toast_count() const;
    [[nodiscard]] const Toast* current_toast() const;

    /// Emitted with the current toast's title when its action runs.
    Signal<std::string_view>& on_action();
    Signal<>& on_dismissed();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;

protected:
    ToastOverlay();
    void snapshot(SnapshotContext& ctx) const override;

private:
    void cancel_timeout();
    void schedule_timeout();
    void pause_timeout();
    void resume_timeout();
    void set_pointer_inside(bool inside);
    void emit_current_action();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
