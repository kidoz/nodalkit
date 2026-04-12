#pragma once

/// @file expander.h
/// @brief Collapsible section with a header.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A collapsible section that reveals its child content when expanded.
class Expander : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Expander> create(std::string title = {});
    ~Expander() override;

    [[nodiscard]] std::string_view title() const;
    void set_title(std::string title);

    [[nodiscard]] bool is_expanded() const;
    void set_expanded(bool expanded);

    /// Set the content widget shown when expanded.
    void set_child(std::shared_ptr<Widget> child);

    Signal<bool>& on_expanded_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    explicit Expander(std::string title);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
