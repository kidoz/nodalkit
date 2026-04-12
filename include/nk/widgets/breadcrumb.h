#pragma once

/// @file breadcrumb.h
/// @brief Path/location navigation bar.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// A breadcrumb trail showing a navigable path.
class Breadcrumb : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Breadcrumb> create();
    ~Breadcrumb() override;

    /// Replace the full path.
    void set_path(std::vector<std::string> segments);

    /// Append a segment.
    void push(std::string segment);

    /// Remove segments after the given depth.
    void pop_to(std::size_t depth);

    [[nodiscard]] std::size_t depth() const;
    [[nodiscard]] std::string_view segment(std::size_t index) const;

    /// Emitted when the user clicks a breadcrumb segment.
    Signal<std::size_t>& on_navigate();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    Breadcrumb();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
