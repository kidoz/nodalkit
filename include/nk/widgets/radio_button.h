#pragma once

/// @file radio_button.h
/// @brief Mutually exclusive selection widget.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

class RadioGroup;

/// A radio button for mutually exclusive selection within a group.
/// Call set_group() on multiple RadioButtons with the same RadioGroup
/// to link them together.
class RadioButton : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<RadioButton> create(std::string label = {});
    ~RadioButton() override;

    [[nodiscard]] std::string_view label() const;
    void set_label(std::string label);

    [[nodiscard]] bool is_selected() const;
    void set_selected(bool selected);

    /// Set the mutual-exclusion group. All buttons sharing the same
    /// group automatically deselect each other.
    void set_group(std::shared_ptr<RadioGroup> group);
    [[nodiscard]] std::shared_ptr<RadioGroup> group() const;

    /// Emitted when this button becomes selected.
    Signal<>& on_selected();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    explicit RadioButton(std::string label);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Groups RadioButtons for mutual exclusion.
class RadioGroup {
public:
    RadioGroup();
    ~RadioGroup();

    [[nodiscard]] static std::shared_ptr<RadioGroup> create();

    void add(RadioButton* button);
    void remove(RadioButton* button);
    void select(RadioButton* button);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
