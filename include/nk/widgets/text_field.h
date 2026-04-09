#pragma once

/// @file text_field.h
/// @brief Single-line text input widget.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <utility>

namespace nk {

/// A single-line text entry field.
class TextField : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<TextField> create(std::string initial_text = {});

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

    /// Caret byte offset within the current UTF-8 buffer.
    [[nodiscard]] std::size_t cursor_position() const;

    /// Inclusive selection start byte offset.
    [[nodiscard]] std::size_t selection_start() const;

    /// Exclusive selection end byte offset.
    [[nodiscard]] std::size_t selection_end() const;

    /// Whether the field currently has a selection.
    [[nodiscard]] bool has_selection() const;

    /// Select the entire current text.
    void select_all();

    /// Signal emitted when the text changes.
    Signal<std::string_view>& on_text_changed();

    /// Signal emitted when the user presses Enter.
    Signal<>& on_activate();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    bool handle_text_input_event(const TextInputEvent& event) override;
    [[nodiscard]] std::optional<Rect> text_input_caret_rect() const;
    [[nodiscard]] CursorShape cursor_shape() const override;
    void on_focus_changed(bool focused) override;

protected:
    explicit TextField(std::string text);
    void snapshot(SnapshotContext& ctx) const override;

private:
    [[nodiscard]] Rect inner_body_rect() const;
    [[nodiscard]] Rect text_rect() const;
    [[nodiscard]] Rect local_text_damage_rect() const;
    [[nodiscard]] std::size_t hit_test_cursor(Point point) const;
    void queue_text_redraw();
    void move_cursor(std::size_t position, bool extend_selection);
    void replace_selection(std::string_view text, bool record_history, bool coalesce_history);
    void ensure_caret_visible();
    void reset_history_grouping();
    void reset_history();
    void push_history_state();
    bool undo();
    bool redo();
    void copy_selection_to_clipboard() const;
    void sync_primary_selection_ownership() const;
    bool delete_backward();
    bool delete_forward();
    bool delete_backward_word();
    bool delete_forward_word();
    bool paste_from_clipboard();
    bool paste_from_primary_selection(std::optional<std::size_t> cursor_position = std::nullopt);
    void clear_preedit();
    void reset_mouse_selection_state();
    bool delete_surrounding_text(std::size_t before_length, std::size_t after_length);
    [[nodiscard]] std::string composed_display_text() const;
    [[nodiscard]] std::size_t display_caret_position() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
