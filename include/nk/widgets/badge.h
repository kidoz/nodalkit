#pragma once

/// @file badge.h
/// @brief Small count or category annotation.

#include <memory>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A small pill-shaped label for counts, status, or categories.
///
/// `create(text)` and `set_text()` draw that string. `create(count)` and
/// `set_count()` draw the number. A count above the cap, 99 by default, is
/// drawn as `99+`. Zero is drawn as `0`. `set_dot(true)` draws a small circle
/// and keeps the accessible name. `set_conveys_status(true)` uses the status
/// role. The default role stays label.
class Badge : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Badge> create(std::string text = {});
    [[nodiscard]] static std::shared_ptr<Badge> create(int count);
    ~Badge() override;

    /// The drawn label. A count badge returns the number or the capped form.
    [[nodiscard]] std::string_view text() const;
    void set_text(std::string text);

    void set_count(int count);
    [[nodiscard]] int count() const;
    void set_count_cap(int cap);
    [[nodiscard]] int count_cap() const;

    void set_dot(bool dot);
    [[nodiscard]] bool is_dot() const;

    void set_conveys_status(bool conveys_status);
    [[nodiscard]] bool conveys_status() const;

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    explicit Badge(std::string text);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void refresh_accessible();
    [[nodiscard]] std::string display_text() const;
};

} // namespace nk
