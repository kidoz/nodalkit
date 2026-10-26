#pragma once

/// @file button.h
/// @brief Clickable button widget.

#include <memory>
#include <nk/actions/action.h>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A push button that emits on_clicked() when activated.
class Button : public Widget {
public:
    /// Create a button with the given label.
    [[nodiscard]] static std::shared_ptr<Button> create(std::string label = {});

    ~Button() override;

    /// Get or set the button label text.
    [[nodiscard]] std::string_view label() const;
    void set_label(std::string label);

    /// Bind shared command state and activation. Null unbinds and restores
    /// sensitivity and unchecked state. The label remains application-owned.
    void set_action(std::shared_ptr<Action> action);
    [[nodiscard]] Action* action() const;

    /// Emitted on activation when no action is bound. Bound buttons activate
    /// their Action instead; observe Action::on_activated() for shared commands.
    Signal<>& on_clicked();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    explicit Button(std::string label);
    void snapshot(SnapshotContext& ctx) const override;

private:
    bool activate();
    void sync_action();

    std::string label_;
    Signal<> clicked_;
    std::shared_ptr<Action> action_;
    ScopedConnection action_changed_;
    bool armed_ = false;
};

} // namespace nk
