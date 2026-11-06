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

    /// Whether the loading animation is requested.
    ///
    /// While this is true, a visible spinner in a visible window advances on
    /// its own timer. Reduced motion, hiding the spinner, hiding the window,
    /// and removing it from the window hold the indicator still. snapshot()
    /// does not advance it.
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
    void on_host_window_changed(Window* window) override;

private:
    void sync_motion();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
