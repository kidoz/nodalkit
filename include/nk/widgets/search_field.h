#pragma once

/// @file search_field.h
/// @brief Text field specialized for search input.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A text input field with search icon and clear button.
class SearchField : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<SearchField> create(std::string placeholder = {});
    ~SearchField() override;

    [[nodiscard]] std::string_view text() const;
    void set_text(std::string text);

    [[nodiscard]] std::string_view placeholder() const;
    void set_placeholder(std::string placeholder);

    /// Emitted when the text changes.
    Signal<std::string_view>& on_text_changed();

    /// Emitted when Enter is pressed.
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
