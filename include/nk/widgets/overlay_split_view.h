#pragma once

/// @file overlay_split_view.h
/// @brief Adaptive sidebar that overlays content at narrow widths.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// Shows sidebar/content side by side until collapsed, then presents the
/// sidebar above the content with a dismissible scrim.
class OverlaySplitView : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<OverlaySplitView> create();
    ~OverlaySplitView() override;

    void set_sidebar(std::shared_ptr<Widget> sidebar);
    [[nodiscard]] Widget* sidebar() const;
    void set_content(std::shared_ptr<Widget> content);
    [[nodiscard]] Widget* content() const;

    [[nodiscard]] bool is_collapsed() const;
    void set_collapsed(bool collapsed);
    [[nodiscard]] bool shows_sidebar() const;
    void set_show_sidebar(bool show_sidebar);

    [[nodiscard]] float sidebar_width() const;
    void set_sidebar_width(float width);

    Signal<bool>& on_collapsed_changed();
    Signal<bool>& on_show_sidebar_changed();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    OverlaySplitView();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
