#pragma once

/// @file text_field.h
/// @brief Single-line text input widget.

#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

#include <memory>
#include <string>
#include <string_view>

namespace nk {

/// A single-line text entry field.
class TextField : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<TextField> create(
        std::string initial_text = {});

    ~TextField() override;

    /// Current text content.
    [[nodiscard]] std::string_view text() const;
    void set_text(std::string text);

    /// Placeholder text shown when empty.
    [[nodiscard]] std::string_view placeholder() const;
    void set_placeholder(std::string placeholder);

    /// Whether the field is editable.
    [[nodiscard]] bool is_editable() const;
    void set_editable(bool editable);

    /// Signal emitted when the text changes.
    Signal<std::string_view>& on_text_changed();

    /// Signal emitted when the user presses Enter.
    Signal<>& on_activate();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(
        Constraints const& constraints) const override;
    bool handle_mouse_event(MouseEvent const& event) override;
    bool handle_key_event(KeyEvent const& event) override;

protected:
    explicit TextField(std::string text);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
