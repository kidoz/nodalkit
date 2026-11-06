#pragma once

/// @file search_bar.h
/// @brief Search button that reveals a SearchField.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A search button that reveals a `SearchField`.
///
/// The bar is a button until `set_revealed(true)`. Revealing shows the field,
/// moves focus into it, and hides the button. Escape or the cancel control
/// collapses the field and returns focus to the button.
///
/// When the allocated width is below the narrow threshold, the revealed field
/// fills the bar. When the bar is wider, the field keeps its fixed width at
/// the trailing edge and the leading space stays empty for the rest of the
/// header. Reduced motion shows and hides the field without a width slide.
/// A wider bar may animate that slide.
///
/// `text()`, `placeholder()`, `on_text_changed()`, and `on_search()` match
/// `SearchField`.
class SearchBar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<SearchBar> create(std::string placeholder = {});
    ~SearchBar() override;

    [[nodiscard]] std::string_view text() const;
    void set_text(std::string text);

    [[nodiscard]] std::string_view placeholder() const;
    void set_placeholder(std::string placeholder);

    [[nodiscard]] bool is_revealed() const;
    void set_revealed(bool revealed);

    Signal<std::string_view>& on_text_changed();
    Signal<std::string_view>& on_search();
    Signal<bool>& on_revealed_changed();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    explicit SearchBar(std::string placeholder);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    [[nodiscard]] bool is_narrow() const;
    [[nodiscard]] bool prefers_reduced_motion() const;
    [[nodiscard]] float target_width() const;
    [[nodiscard]] float shown_width() const;
    [[nodiscard]] Rect cluster_rect() const;
    [[nodiscard]] Rect cancel_rect() const;
    void layout_contents();
    void start_reveal_animation();
    void cancel_reveal_timer();
};

} // namespace nk
