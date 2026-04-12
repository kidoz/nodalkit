#pragma once

/// @file calendar.h
/// @brief Month-view date picker widget.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

namespace nk {

/// A simple date represented as year/month/day.
struct Date {
    int year = 2026;
    int month = 1; ///< 1-12
    int day = 1;   ///< 1-31

    constexpr bool operator==(Date const&) const = default;
};

/// A month-view calendar for date selection.
class Calendar : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Calendar> create();
    ~Calendar() override;

    [[nodiscard]] Date selected_date() const;
    void set_selected_date(Date date);

    /// The month currently displayed.
    [[nodiscard]] int displayed_month() const;
    [[nodiscard]] int displayed_year() const;
    void set_displayed_month(int year, int month);

    /// Navigate months.
    void go_previous_month();
    void go_next_month();

    Signal<Date>& on_date_selected();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] CursorShape cursor_shape() const override;

protected:
    Calendar();
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
