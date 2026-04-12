#pragma once

/// @file check_box.h
/// @brief Boolean toggle widget with a label.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A check box that toggles between checked and unchecked states.
class CheckBox : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<CheckBox> create(std::string label = {});
    ~CheckBox() override;

    [[nodiscard]] std::string_view label() const;
    void set_label(std::string label);

    [[nodiscard]] bool is_checked() const;
    void set_checked(bool checked);

    /// Emitted when the checked state changes.
    Signal<bool>& on_toggled();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    explicit CheckBox(std::string label);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
