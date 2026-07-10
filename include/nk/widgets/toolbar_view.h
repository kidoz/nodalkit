#pragma once

/// @file toolbar_view.h
/// @brief Page content with coordinated top and bottom bars.

#include <memory>
#include <nk/ui_core/widget.h>
#include <span>
#include <vector>

namespace nk {

enum class ToolbarStyle {
    Flat,
    Raised,
    RaisedBorder,
};

/// Coordinates one page's content with one or more top and bottom bars.
class ToolbarView : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ToolbarView> create();
    ~ToolbarView() override;

    void set_content(std::shared_ptr<Widget> content);
    [[nodiscard]] Widget* content() const;

    void add_top_bar(std::shared_ptr<Widget> bar);
    void add_bottom_bar(std::shared_ptr<Widget> bar);
    void remove(Widget& bar);
    [[nodiscard]] std::span<const std::shared_ptr<Widget>> top_bars() const;
    [[nodiscard]] std::span<const std::shared_ptr<Widget>> bottom_bars() const;

    [[nodiscard]] ToolbarStyle top_bar_style() const;
    void set_top_bar_style(ToolbarStyle style);
    [[nodiscard]] ToolbarStyle bottom_bar_style() const;
    void set_bottom_bar_style(ToolbarStyle style);

    [[nodiscard]] bool reveals_top_bars() const;
    void set_reveal_top_bars(bool reveal);
    [[nodiscard]] bool reveals_bottom_bars() const;
    void set_reveal_bottom_bars(bool reveal);

    [[nodiscard]] bool extends_content_to_top_edge() const;
    void set_extend_content_to_top_edge(bool extend);
    [[nodiscard]] bool extends_content_to_bottom_edge() const;
    void set_extend_content_to_bottom_edge(bool extend);

    [[nodiscard]] float top_bar_height() const;
    [[nodiscard]] float bottom_bar_height() const;

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;

protected:
    ToolbarView();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
