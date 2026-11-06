#pragma once

/// @file tab_bar.h
/// @brief Tabbed page switching widget.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/button_content.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// A horizontal row of tabs for page switching.
///
/// Each tab has a label and may have an icon, a close button, and one child
/// page. Tabs that do not fit stay in an overflow menu. The selected tab's
/// page is shown under the bar. `on_selection_changed()` reports the selected
/// index.
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

    void set_tab_icon(std::size_t index, SymbolicIcon icon);
    [[nodiscard]] SymbolicIcon tab_icon(std::size_t index) const;

    void set_tab_closable(std::size_t index, bool closable);
    [[nodiscard]] bool is_tab_closable(std::size_t index) const;

    /// The page shown while this tab is selected. Null clears it.
    void set_tab_page(std::size_t index, std::shared_ptr<Widget> page);
    [[nodiscard]] Widget* tab_page(std::size_t index) const;

    [[nodiscard]] int selected_index() const;
    void set_selected_index(int index);

    Signal<int>& on_selection_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] std::vector<Rect> damage_regions() const override;
    [[nodiscard]] CursorShape cursor_shape() const override;
    void on_focus_changed(bool focused) override;

protected:
    TabBar();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Tab {
        std::string label;
        SymbolicIcon icon = SymbolicIcon::None;
        bool closable = false;
        std::shared_ptr<Widget> page;
    };

    struct PlacementItem {
        int index = -1;
        Rect bounds{};
        Rect close{};
    };

    struct Placement {
        std::vector<PlacementItem> visible;
        Rect overflow{};
        bool has_overflow = false;
    };

    struct Impl;
    std::unique_ptr<Impl> impl_;

    [[nodiscard]] Tab& tab_at(std::size_t index);
    [[nodiscard]] const Tab& tab_at(std::size_t index) const;
    void sync_pages();
    [[nodiscard]] float tab_width(const Tab& tab) const;
    [[nodiscard]] float bar_height() const;
    [[nodiscard]] Placement placement() const;
    void open_overflow();
};

} // namespace nk
