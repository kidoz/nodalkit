#pragma once

/// @file text_area.h
/// @brief Multi-line text editor widget.

#include <cstddef>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A multi-line text editing area with an unwrapped, scrollable viewport.
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

    /// Caret byte offset within the current UTF-8 buffer.
    [[nodiscard]] std::size_t cursor_position() const;
    /// Inclusive selection start and exclusive selection end, in UTF-8 bytes.
    [[nodiscard]] std::size_t selection_start() const;
    [[nodiscard]] std::size_t selection_end() const;
    [[nodiscard]] bool has_selection() const;
    /// Select the entire document, including in read-only mode.
    void select_all();
    /// Caret bounds in window coordinates, accounting for viewport scrolling.
    /// The bounds can lie outside the viewport after manual scrolling.
    [[nodiscard]] Rect text_input_caret_rect() const;

    /// Number of visible rows for size hint.
    [[nodiscard]] int visible_rows() const;
    void set_visible_rows(int rows);

    Signal<>& on_text_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    bool handle_text_input_event(const TextInputEvent& event) override;
    /// Committed surrounding text and byte offsets, plus the visible composition caret.
    [[nodiscard]] std::optional<WidgetTextInputState> text_input_state() const override;
    [[nodiscard]] CursorShape cursor_shape() const override;
    void on_focus_changed(bool focused) override;

protected:
    TextArea();
    void snapshot(SnapshotContext& ctx) const override;

private:
    [[nodiscard]] Rect text_rect() const;
    [[nodiscard]] float line_height() const;
    [[nodiscard]] std::size_t cursor_line() const;
    [[nodiscard]] std::size_t position_at_x(std::string_view line, float x) const;
    [[nodiscard]] std::size_t hit_test_cursor(Point point) const;
    void refresh_content_metrics();
    void clamp_scroll();
    void ensure_caret_visible();
    void did_edit();
    void did_select();
    void extend_mouse_selection(Point point);
    void sync_primary_selection() const;
    bool replace_selection(std::string_view text, bool typing = false);
    bool clear_preedit();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
