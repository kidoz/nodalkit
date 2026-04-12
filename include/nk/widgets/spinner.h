#pragma once

/// @file spinner.h
/// @brief Indeterminate loading indicator.

#include <memory>
#include <nk/ui_core/widget.h>

namespace nk {

/// An animated spinning indicator for indeterminate loading states.
class Spinner : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Spinner> create();
    ~Spinner() override;

    /// Whether the spinner is actively animating.
    [[nodiscard]] bool is_spinning() const;
    void set_spinning(bool spinning);

    /// Diameter in logical pixels.
    [[nodiscard]] float diameter() const;
    void set_diameter(float diameter);

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    Spinner();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
