#pragma once

/// @file tab_bar.h
/// @brief Tabbed page switching widget.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// A horizontal row of tabs for page switching.
class TabBar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<TabBar> create();
    ~TabBar() override;

    void append_tab(std::string label);
    void insert_tab(std::size_t index, std::string label);
    void remove_tab(std::size_t index);
    void clear_tabs();

    [[nodiscard]] std::size_t tab_count() const;
    [[nodiscard]] std::string_view tab_label(std::size_t index) const;
    void set_tab_label(std::size_t index, std::string label);

    [[nodiscard]] int selected_index() const;
    void set_selected_index(int index);

    Signal<int>& on_selection_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;
    void on_focus_changed(bool focused) override;

protected:
    TabBar();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
