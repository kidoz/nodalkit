#pragma once

/// @file window_title.h
/// @brief Title and subtitle heading for a header bar.

#include <memory>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A window heading: one title line, and an optional smaller subtitle.
///
/// An empty subtitle leaves the title vertically centered. `Headerbar` can
/// center this widget, or any other widget, in place of its title string.
class WindowTitle : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<WindowTitle> create(std::string title = {},
                                                             std::string subtitle = {});
    ~WindowTitle() override;

    [[nodiscard]] std::string_view title() const;
    void set_title(std::string title);

    [[nodiscard]] std::string_view subtitle() const;
    void set_subtitle(std::string subtitle);

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    WindowTitle(std::string title, std::string subtitle);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
