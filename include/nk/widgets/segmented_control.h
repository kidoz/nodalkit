#pragma once

/// @file segmented_control.h
/// @brief Compact segmented selector widget for switching between peer views.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// Compact segmented selector for switching between a small set of peer options.
class SegmentedControl : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<SegmentedControl> create();
    ~SegmentedControl() override;

    /// Replace the available segments. Selects the first segment when the
    /// control becomes non-empty and the current selection is invalid.
    void set_segments(std::vector<std::string> segments);

    /// Number of segments currently available.
    [[nodiscard]] std::size_t segment_count() const;

    /// Get a segment label by index.
    [[nodiscard]] std::string_view segment(std::size_t index) const;

    /// Currently selected segment index (-1 when empty).
    [[nodiscard]] int selected_index() const;
    void set_selected_index(int index);

    /// Current segment label ("" when empty).
    [[nodiscard]] std::string_view selected_text() const;

    /// Emitted when the selection changes.
    Signal<int>& on_selection_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;
    void on_focus_changed(bool focused) override;

protected:
    SegmentedControl();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
