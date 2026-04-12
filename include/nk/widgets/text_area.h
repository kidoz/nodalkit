#pragma once

/// @file text_area.h
/// @brief Multi-line text editor widget.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A multi-line, scrollable text editing area.
class TextArea : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<TextArea> create();
    ~TextArea() override;

    [[nodiscard]] std::string_view text() const;
    void set_text(std::string text);

    [[nodiscard]] std::string_view placeholder() const;
    void set_placeholder(std::string placeholder);

    [[nodiscard]] bool is_editable() const;
    void set_editable(bool editable);

    /// Number of visible rows for size hint.
    [[nodiscard]] int visible_rows() const;
    void set_visible_rows(int rows);

    Signal<>& on_text_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    bool handle_text_input_event(const TextInputEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;
    void on_focus_changed(bool focused) override;

protected:
    TextArea();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
