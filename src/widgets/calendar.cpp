#include <algorithm>
#include <cmath>
#include <sstream>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/calendar.h>

namespace nk {

namespace {

FontDescriptor calendar_font() {
    return FontDescriptor{
        .family = {},
        .size = 12.0F,
        .weight = FontWeight::Regular,
    };
}

FontDescriptor calendar_header_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

FontDescriptor calendar_bold_font() {
    return FontDescriptor{
        .family = {},
        .size = 12.0F,
        .weight = FontWeight::Bold,
    };
}

bool is_leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int days_in_month(int year, int month) {
    static constexpr int days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && is_leap_year(year)) {
        return 29;
    }
    return days[month];
}

/// Returns 0=Sunday .. 6=Saturday using Tomohiko Sakamoto's algorithm.
int day_of_week(int year, int month, int day) {
    static constexpr int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    int y = year;
    if (month < 3) {
        --y;
    }
    return (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
}

const char* month_name(int month) {
    static constexpr const char* names[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December",
    };
    return names[month];
}

} // namespace

struct Calendar::Impl {
    Date selected_date;
    int display_year = 2026;
    int display_month = 1;
    Signal<Date> date_selected;
    int hovered_day = -1;
};

std::shared_ptr<Calendar> Calendar::create() {
    return std::shared_ptr<Calendar>(new Calendar());
}

Calendar::Calendar() : impl_(std::make_unique<Impl>()) {
    impl_->selected_date = Date{2026, 1, 1};
    impl_->display_year = impl_->selected_date.year;
    impl_->display_month = impl_->selected_date.month;
    set_focusable(true);
    add_style_class("calendar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Grid);
    accessible.set_name("Calendar");
}

Calendar::~Calendar() = default;

Date Calendar::selected_date() const {
    return impl_->selected_date;
}

void Calendar::set_selected_date(Date date) {
    if (!(impl_->selected_date == date)) {
        impl_->selected_date = date;
        queue_redraw();
    }
}

int Calendar::displayed_month() const {
    return impl_->display_month;
}

int Calendar::displayed_year() const {
    return impl_->display_year;
}

void Calendar::set_displayed_month(int year, int month) {
    if (impl_->display_year != year || impl_->display_month != month) {
        impl_->display_year = year;
        impl_->display_month = month;
        queue_redraw();
    }
}

void Calendar::go_previous_month() {
    impl_->display_month--;
    if (impl_->display_month < 1) {
        impl_->display_month = 12;
        impl_->display_year--;
    }
    queue_redraw();
}

void Calendar::go_next_month() {
    impl_->display_month++;
    if (impl_->display_month > 12) {
        impl_->display_month = 1;
        impl_->display_year++;
    }
    queue_redraw();
}

Signal<Date>& Calendar::on_date_selected() {
    return impl_->date_selected;
}

SizeRequest Calendar::measure(const Constraints& /*constraints*/) const {
    const float cell_size = theme_number("cell-size", 32.0F);
    const float header_height = theme_number("header-height", 32.0F);
    const float dow_height = theme_number("dow-height", 24.0F);
    const float w = cell_size * 7.0F;
    const float h = header_height + dow_height + cell_size * 6.0F;
    return {w, h, w, h};
}

bool Calendar::handle_mouse_event(const MouseEvent& event) {
    const auto a = allocation();
    const float cell_size = theme_number("cell-size", 32.0F);
    const float header_height = theme_number("header-height", 32.0F);
    const float dow_height = theme_number("dow-height", 24.0F);

    const float rel_x = event.x - a.x;
    const float rel_y = event.y - a.y;

    switch (event.type) {
    case MouseEvent::Type::Press: {
        if (event.button != 1) {
            return false;
        }

        // Check navigation arrows in header row.
        if (rel_y >= 0.0F && rel_y < header_height) {
            if (rel_x >= 0.0F && rel_x < cell_size) {
                go_previous_month();
                return true;
            }
            if (rel_x >= a.width - cell_size && rel_x < a.width) {
                go_next_month();
                return true;
            }
            return false;
        }

        // Check day grid.
        const float grid_y = rel_y - header_height - dow_height;
        if (grid_y < 0.0F) {
            return false;
        }

        const int col = static_cast<int>(rel_x / cell_size);
        const int row = static_cast<int>(grid_y / cell_size);
        if (col < 0 || col >= 7 || row < 0 || row >= 6) {
            return false;
        }

        const int first_dow = day_of_week(impl_->display_year, impl_->display_month, 1);
        const int day_index = row * 7 + col - first_dow + 1;
        const int total_days = days_in_month(impl_->display_year, impl_->display_month);
        if (day_index >= 1 && day_index <= total_days) {
            impl_->selected_date = Date{impl_->display_year, impl_->display_month, day_index};
            impl_->date_selected.emit(impl_->selected_date);
            queue_redraw();
            return true;
        }
        return false;
    }

    case MouseEvent::Type::Move: {
        const float grid_y = rel_y - header_height - dow_height;
        int new_hovered = -1;
        if (grid_y >= 0.0F) {
            const int col = static_cast<int>(rel_x / cell_size);
            const int row = static_cast<int>(grid_y / cell_size);
            if (col >= 0 && col < 7 && row >= 0 && row < 6) {
                const int first_dow = day_of_week(impl_->display_year, impl_->display_month, 1);
                const int day_index = row * 7 + col - first_dow + 1;
                const int total_days = days_in_month(impl_->display_year, impl_->display_month);
                if (day_index >= 1 && day_index <= total_days) {
                    new_hovered = day_index;
                }
            }
        }
        if (new_hovered != impl_->hovered_day) {
            impl_->hovered_day = new_hovered;
            queue_redraw();
        }
        return false;
    }

    case MouseEvent::Type::Leave:
        if (impl_->hovered_day != -1) {
            impl_->hovered_day = -1;
            queue_redraw();
        }
        return false;

    case MouseEvent::Type::Release:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool Calendar::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    switch (event.key) {
    case KeyCode::Left: {
        auto& d = impl_->selected_date;
        d.day--;
        if (d.day < 1) {
            d.month--;
            if (d.month < 1) {
                d.month = 12;
                d.year--;
            }
            d.day = days_in_month(d.year, d.month);
        }
        impl_->display_year = d.year;
        impl_->display_month = d.month;
        impl_->date_selected.emit(d);
        queue_redraw();
        return true;
    }
    case KeyCode::Right: {
        auto& d = impl_->selected_date;
        d.day++;
        const int total = days_in_month(d.year, d.month);
        if (d.day > total) {
            d.day = 1;
            d.month++;
            if (d.month > 12) {
                d.month = 1;
                d.year++;
            }
        }
        impl_->display_year = d.year;
        impl_->display_month = d.month;
        impl_->date_selected.emit(d);
        queue_redraw();
        return true;
    }
    case KeyCode::Up: {
        auto& d = impl_->selected_date;
        d.day -= 7;
        if (d.day < 1) {
            d.month--;
            if (d.month < 1) {
                d.month = 12;
                d.year--;
            }
            d.day += days_in_month(d.year, d.month);
        }
        impl_->display_year = d.year;
        impl_->display_month = d.month;
        impl_->date_selected.emit(d);
        queue_redraw();
        return true;
    }
    case KeyCode::Down: {
        auto& d = impl_->selected_date;
        d.day += 7;
        const int total = days_in_month(d.year, d.month);
        if (d.day > total) {
            d.day -= total;
            d.month++;
            if (d.month > 12) {
                d.month = 1;
                d.year++;
            }
        }
        impl_->display_year = d.year;
        impl_->display_month = d.month;
        impl_->date_selected.emit(d);
        queue_redraw();
        return true;
    }
    case KeyCode::Return: {
        impl_->date_selected.emit(impl_->selected_date);
        return true;
    }
    default:
        return false;
    }
}

CursorShape Calendar::cursor_shape() const {
    return CursorShape::PointingHand;
}

void Calendar::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float cell_size = theme_number("cell-size", 32.0F);
    const float header_height = theme_number("header-height", 32.0F);
    const float dow_height = theme_number("dow-height", 24.0F);
    const float corner_radius = theme_number("corner-radius", 8.0F);

    // Background.
    ctx.add_rounded_rect(a, theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}),
                         corner_radius);
    ctx.add_border(a, theme_color("border-color", Color{0.86F, 0.88F, 0.91F, 1.0F}), 1.0F,
                   corner_radius);

    const auto header_font = calendar_header_font();
    const auto font = calendar_font();

    // Header: "< Month Year >"
    const std::string prev_arrow = "\xe2\x80\xb9"; // single left-pointing angle quotation mark
    const std::string next_arrow = "\xe2\x80\xba"; // single right-pointing angle quotation mark

    const auto prev_measured = measure_text(prev_arrow, header_font);
    const float prev_x = a.x + (cell_size - prev_measured.width) * 0.5F;
    const float prev_y = a.y + (header_height - prev_measured.height) * 0.5F;
    ctx.add_text({prev_x, prev_y}, std::string(prev_arrow),
                 theme_color("text-color", Color{0.2F, 0.2F, 0.2F, 1.0F}), header_font);

    const auto next_measured = measure_text(next_arrow, header_font);
    const float next_x = a.x + a.width - cell_size + (cell_size - next_measured.width) * 0.5F;
    const float next_y = a.y + (header_height - next_measured.height) * 0.5F;
    ctx.add_text({next_x, next_y}, std::string(next_arrow),
                 theme_color("text-color", Color{0.2F, 0.2F, 0.2F, 1.0F}), header_font);

    std::ostringstream title_ss;
    title_ss << month_name(impl_->display_month) << " " << impl_->display_year;
    const std::string title = title_ss.str();
    const auto title_measured = measure_text(title, header_font);
    const float title_x = a.x + (a.width - title_measured.width) * 0.5F;
    const float title_y = a.y + (header_height - title_measured.height) * 0.5F;
    ctx.add_text({title_x, title_y}, std::string(title),
                 theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F}), header_font);

    // Day-of-week header.
    static constexpr const char* dow_labels[] = {"S", "M", "T", "W", "T", "F", "S"};
    const Color dow_color = theme_color("secondary-text-color", Color{0.5F, 0.5F, 0.5F, 1.0F});
    for (int i = 0; i < 7; ++i) {
        const auto m = measure_text(dow_labels[i], font);
        const float cx = a.x + static_cast<float>(i) * cell_size + (cell_size - m.width) * 0.5F;
        const float cy = a.y + header_height + (dow_height - m.height) * 0.5F;
        ctx.add_text({cx, cy}, std::string(dow_labels[i]), dow_color, font);
    }

    // Day grid.
    const int first_dow = day_of_week(impl_->display_year, impl_->display_month, 1);
    const int total_days = days_in_month(impl_->display_year, impl_->display_month);
    const float grid_top = a.y + header_height + dow_height;

    const Color accent_bg = theme_color("accent-color", Color{0.3F, 0.56F, 0.9F, 1.0F});
    const Color hover_bg = theme_color("hover-color", Color{0.9F, 0.92F, 0.95F, 1.0F});
    const Color day_text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    const Color selected_text_color = theme_color("selected-text-color", Color{1.0F, 1.0F, 1.0F, 1.0F});

    const bool selected_in_view = (impl_->selected_date.year == impl_->display_year &&
                                   impl_->selected_date.month == impl_->display_month);

    for (int day = 1; day <= total_days; ++day) {
        const int cell_index = first_dow + day - 1;
        const int col = cell_index % 7;
        const int row = cell_index / 7;

        const float cx = a.x + static_cast<float>(col) * cell_size;
        const float cy = grid_top + static_cast<float>(row) * cell_size;

        const bool is_selected = selected_in_view && impl_->selected_date.day == day;
        const bool is_hovered = impl_->hovered_day == day;

        // Highlight background.
        const float dot_radius = cell_size * 0.4F;
        const float dot_x = cx + (cell_size - dot_radius * 2.0F) * 0.5F;
        const float dot_y = cy + (cell_size - dot_radius * 2.0F) * 0.5F;
        const Rect dot_rect{dot_x, dot_y, dot_radius * 2.0F, dot_radius * 2.0F};

        if (is_selected) {
            ctx.add_rounded_rect(dot_rect, accent_bg, dot_radius);
        } else if (is_hovered) {
            ctx.add_rounded_rect(dot_rect, hover_bg, dot_radius);
        }

        // Day number.
        const std::string day_str = std::to_string(day);
        const auto day_font = is_selected ? calendar_bold_font() : font;
        const auto dm = measure_text(day_str, day_font);
        const float dx = cx + (cell_size - dm.width) * 0.5F;
        const float dy = cy + (cell_size - dm.height) * 0.5F;
        ctx.add_text({dx, dy}, std::string(day_str),
                     is_selected ? selected_text_color : day_text_color, day_font);
    }

    // Focus ring.
    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_border(a, theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}), 2.0F,
                       corner_radius);
    }
}

} // namespace nk
