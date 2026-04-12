#pragma once

/// @file separator.h
/// @brief Visual divider line widget.

#include <memory>
#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// A horizontal or vertical separator line.
class Separator : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Separator> create(
        Orientation orientation = Orientation::Horizontal);
    ~Separator() override;

    [[nodiscard]] Orientation orientation() const;

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    explicit Separator(Orientation orientation);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
