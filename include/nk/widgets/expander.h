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
///
/// An optional subtitle sits under the title. An empty subtitle leaves the
/// header as one line. `set_suffix()` places one widget at the trailing side
/// of the header. A click on the suffix does not toggle the expander. A click
/// on the title, or Return or Space, still does. The child stays below the
/// header and appears only while the expander is open.
class Expander : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Expander> create(std::string title = {});
    ~Expander() override;

    [[nodiscard]] std::string_view title() const;
    void set_title(std::string title);
    [[nodiscard]] std::string_view subtitle() const;
    void set_subtitle(std::string subtitle);

    /// A trailing header widget, such as a value or a switch. Null clears it.
    void set_suffix(std::shared_ptr<Widget> suffix);
    [[nodiscard]] Widget* suffix() const;

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

    [[nodiscard]] float header_height() const;
};

} // namespace nk
