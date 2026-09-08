#pragma once

/// @file search_field.h
/// @brief Text field specialized for search input.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/widgets/text_field.h>
#include <string>
#include <string_view>

namespace nk {

/// A single-line editor with a search icon and an undoable clear control.
/// Inherits TextField selection, clipboard, undo/redo, and text-input behavior.
class SearchField : public TextField {
public:
    [[nodiscard]] static std::shared_ptr<SearchField> create(std::string placeholder = {});
    ~SearchField() override;

    [[nodiscard]] std::string_view text() const;
    void set_text(std::string text);

    [[nodiscard]] std::string_view placeholder() const;
    void set_placeholder(std::string placeholder);

    /// Emitted when the text changes.
    Signal<std::string_view>& on_text_changed();

    /// Emitted when Enter is pressed, including activation through TextField.
    Signal<std::string_view>& on_search();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    bool handle_text_input_event(const TextInputEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;
    void on_focus_changed(bool focused) override;

protected:
    explicit SearchField(std::string placeholder);
    void snapshot(SnapshotContext& ctx) const override;
    [[nodiscard]] Rect text_rect() const override;

private:
    [[nodiscard]] Rect clear_button_rect() const;
    bool clear_query();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
