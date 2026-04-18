#pragma once

/// @file label.h
/// @brief Read-only text display widget.

#include <nk/ui_core/widget.h>

#include <memory>
#include <string>
#include <string_view>

namespace nk {

/// Displays a single-line or multi-line text label.
class Label : public Widget {
public:
    /// Create a label with the given text.
    [[nodiscard]] static std::shared_ptr<Label> create(
        std::string text = {});

    ~Label() override;

    /// Get or set the displayed text.
    [[nodiscard]] std::string_view text() const;
    void set_text(std::string text);

    /// Horizontal text alignment within the label area.
    [[nodiscard]] HAlign h_align() const;
    void set_h_align(HAlign align);

    /// Whether text wraps at the widget's width.
    [[nodiscard]] bool wrapping() const;
    void set_wrapping(bool enabled);

    // --- Widget overrides ---
    [[nodiscard]] bool has_height_for_width() const override;
    [[nodiscard]] float height_for_width(float width) const override;

    [[nodiscard]] SizeRequest measure(
        Constraints const& constraints) const override;

protected:
    explicit Label(std::string text);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
