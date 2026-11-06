#pragma once

/// @file busy_overlay.h
/// @brief Scrim, spinner, and message over one page.

#include <memory>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// Covers one page while work is in progress.
///
/// The page stays visible behind a scrim. While busy, that page does not
/// receive pointer or keyboard input. The scrim shows a `Spinner` and a short
/// message. An empty message shows the spinner alone. `set_busy(false)`
/// removes the scrim and returns input to the page. This is not a dialog: it
/// has no buttons and does not change the window's modal state.
class BusyOverlay : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<BusyOverlay> create();
    ~BusyOverlay() override;

    void set_child(std::shared_ptr<Widget> page);
    [[nodiscard]] Widget* child() const;

    [[nodiscard]] bool is_busy() const;
    void set_busy(bool busy);

    [[nodiscard]] std::string_view message() const;
    void set_message(std::string message);

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    BusyOverlay();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    [[nodiscard]] bool contains_page(const Widget* widget) const;
    void place_scrim_contents();
};

} // namespace nk
