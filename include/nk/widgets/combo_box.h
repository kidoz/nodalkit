#pragma once

/// @file combo_box.h
/// @brief Drop-down selection widget.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// Drop-down selection widget.
class ComboBox : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ComboBox> create();
    ~ComboBox() override;

    /// Set the list of options.
    void set_items(std::vector<std::string> items);

    /// Number of items in the list.
    [[nodiscard]] std::size_t item_count() const;

    /// Get item text by index.
    [[nodiscard]] std::string_view item(std::size_t index) const;

    /// Currently selected index (-1 for none).
    [[nodiscard]] int selected_index() const;
    void set_selected_index(int index);

    /// Text of the currently selected item ("" if none).
    [[nodiscard]] std::string_view selected_text() const;

    /// Emitted when selection changes.
    Signal<int>& on_selection_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] std::vector<Rect> damage_regions() const override;
    [[nodiscard]] CursorShape cursor_shape() const override;
    void on_focus_changed(bool focused) override;

protected:
    ComboBox();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
