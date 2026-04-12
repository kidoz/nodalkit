#pragma once

/// @file tooltip.h
/// @brief Informational popup shown on hover.

#include <memory>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A non-interactive overlay that displays informational text.
/// Typically shown by a parent widget in response to hover events.
class Tooltip : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Tooltip> create(std::string text = {});
    ~Tooltip() override;

    [[nodiscard]] std::string_view text() const;
    void set_text(std::string text);

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    explicit Tooltip(std::string text);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
