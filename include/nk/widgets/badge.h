#pragma once

/// @file badge.h
/// @brief Small count or category annotation.

#include <memory>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A small pill-shaped label for counts, status, or categories.
class Badge : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Badge> create(std::string text = {});
    ~Badge() override;

    [[nodiscard]] std::string_view text() const;
    void set_text(std::string text);

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    explicit Badge(std::string text);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
