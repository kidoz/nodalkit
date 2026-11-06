#pragma once

/// @file link_button.h
/// @brief Inline text link.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// An inline link. It is only as wide as its label.
///
/// A click, Return, or Space activates the link. The signal carries the
/// target when one was set, and the label when no target was set. The button
/// does not open a URL. The application decides what the activation does.
/// The accessible role is `Link` and the accessible name is the label.
class LinkButton : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<LinkButton> create(std::string label = {},
                                                            std::string target = {});
    ~LinkButton() override;

    [[nodiscard]] std::string_view label() const;
    void set_label(std::string label);

    /// Destination reported on activation. Empty means the label is reported.
    [[nodiscard]] std::string_view target() const;
    void set_target(std::string target);

    Signal<std::string_view>& on_activated();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    LinkButton(std::string label, std::string target);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void activate();
};

} // namespace nk
