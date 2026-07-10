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
    Signal<>& on_action();
    Signal<>& on_dismissed();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;

protected:
    ToastOverlay();
    void snapshot(SnapshotContext& ctx) const override;

private:
    void cancel_timeout();
    void schedule_timeout();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
