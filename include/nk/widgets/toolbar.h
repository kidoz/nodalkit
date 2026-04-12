#pragma once

/// @file toolbar.h
/// @brief Horizontal container for action buttons and controls.

#include <memory>
#include <nk/ui_core/widget.h>

namespace nk {

/// A horizontal toolbar that arranges child widgets in a row.
/// Children are added with append_child via the public API.
class Toolbar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Toolbar> create();
    ~Toolbar() override;

    /// Add a widget to the toolbar.
    void add_item(std::shared_ptr<Widget> item);

    /// Insert a visual separator at the current position.
    void add_separator();

    /// Remove all items.
    void clear_items();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    Toolbar();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
