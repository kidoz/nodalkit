#include "candlestick_chart.h"

#include <algorithm>
#include <cmath>
#include <nk/accessibility/accessible.h>
#include <nk/accessibility/role.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <string>

namespace trading {

namespace {

constexpr int MinVisibleCandles = 12;
constexpr int MaxVisibleCandles = 96;

nk::FontDescriptor chart_font(float size = 11.0F, nk::FontWeight weight = nk::FontWeight::Regular) {
    return {
        .family = {},
        .size = size,
        .weight = weight,
    };
}

} // namespace

std::shared_ptr<CandlestickChart> CandlestickChart::create() {
    return std::shared_ptr<CandlestickChart>(new CandlestickChart());
}

CandlestickChart::CandlestickChart() {
    add_style_class("trading-chart");
    auto& accessible = ensure_accessible();
    accessible.set_role(nk::AccessibleRole::Image);
    accessible.set_name("Candlestick chart");
    accessible.set_description(
        "Use Left and Right to inspect candles, Equals and Minus to zoom, and Escape to clear "
        "the inspection cursor.");
    accessible.add_action(nk::AccessibleAction::Focus, [this] {
        grab_focus();
        return true;
    });
    draw_connection_ = nk::ScopedConnection(on_draw().connect(
        [this](nk::SnapshotContext& ctx, nk::Rect bounds) { draw(ctx, bounds); }));
}

CandlestickChart::~CandlestickChart() = default;

void CandlestickChart::set_data(std::string symbol, std::vector<Candle> candles) {
    if (symbol_ != symbol) {
        cursor_index_.reset();
    }
    symbol_ = std::move(symbol);
    candles_ = std::move(candles);
    if (cursor_index_ && *cursor_index_ >= candles_.size()) {
        cursor_index_.reset();
    }
    update_accessible_summary();
    queue_canvas_redraw();
}

void CandlestickChart::zoom_in() {
    visible_count_ = std::max(MinVisibleCandles, visible_count_ - 8);
    queue_canvas_redraw();
}

void CandlestickChart::zoom_out() {
    visible_count_ = std::min(MaxVisibleCandles, visible_count_ + 8);
    queue_canvas_redraw();
}

void CandlestickChart::reset_view() {
    visible_count_ = 48;
    cursor_index_.reset();
    update_accessible_summary();
    queue_canvas_redraw();
}

int CandlestickChart::visible_candle_count() const {
    return static_cast<int>(
        std::min<std::size_t>(candles_.size(), static_cast<std::size_t>(visible_count_)));
}

std::optional<std::size_t> CandlestickChart::cursor_index() const {
    return cursor_index_;
}

nk::SizeRequest CandlestickChart::measure(const nk::Constraints& /*constraints*/) const {
    return {320.0F, 260.0F, 640.0F, 440.0F};
}

bool CandlestickChart::handle_mouse_event(const nk::MouseEvent& event) {
    if (event.type == nk::MouseEvent::Type::Scroll) {
        if (event.scroll_dy > 0.0F) {
            zoom_in();
        } else if (event.scroll_dy < 0.0F) {
            zoom_out();
        }
        return true;
    }
    if (event.type == nk::MouseEvent::Type::Leave) {
        set_cursor(std::nullopt);
        return true;
    }
    if (event.type != nk::MouseEvent::Type::Move && event.type != nk::MouseEvent::Type::Press) {
        return false;
    }

    const auto plot = plot_rect(allocation());
    const int count = visible_candle_count();
    if (count == 0 || !plot.contains({event.x, event.y})) {
        return false;
    }
    if (event.type == nk::MouseEvent::Type::Press) {
        grab_focus();
    }

    const float step = plot.width / static_cast<float>(count);
    const auto offset = std::clamp(static_cast<int>((event.x - plot.x) / step), 0, count - 1);
    set_cursor(first_visible_index() + static_cast<std::size_t>(offset));
    return true;
}

bool CandlestickChart::handle_key_event(const nk::KeyEvent& event) {
    if (event.type != nk::KeyEvent::Type::Press) {
        return false;
    }

    switch (event.key) {
    case nk::KeyCode::Equals:
    case nk::KeyCode::NumpadPlus:
        zoom_in();
        return true;
    case nk::KeyCode::Minus:
    case nk::KeyCode::NumpadMinus:
        zoom_out();
        return true;
    case nk::KeyCode::Escape:
        set_cursor(std::nullopt);
        return true;
    case nk::KeyCode::Left:
    case nk::KeyCode::Right:
    case nk::KeyCode::Home:
    case nk::KeyCode::End: {
        if (candles_.empty()) {
            return false;
        }
        std::size_t index = cursor_index_.value_or(candles_.size() - 1);
        if (event.key == nk::KeyCode::Left && index > 0) {
            --index;
        } else if (event.key == nk::KeyCode::Right && index + 1 < candles_.size()) {
            ++index;
        } else if (event.key == nk::KeyCode::Home) {
            index = first_visible_index();
        } else if (event.key == nk::KeyCode::End) {
            index = candles_.size() - 1;
        }
        set_cursor(index);
        return true;
    }
    default:
        return false;
    }
}

void CandlestickChart::draw(nk::SnapshotContext& ctx, nk::Rect bounds) {
    const auto background = theme_color("background", nk::Color{1.0F, 1.0F, 1.0F, 1.0F});
    const auto border = theme_color("border-color");
    const auto text = theme_color("text-color");
    const auto muted = theme_color("muted-text-color");
    const auto accent = theme_color("accent-color");
    const nk::Color grid{border.r, border.g, border.b, 0.55F};
    const nk::Color rising = nk::Color::from_rgb(22, 163, 74);
    const nk::Color falling = nk::Color::from_rgb(220, 38, 38);

    ctx.add_rounded_rect(bounds, background, 8.0F);
    ctx.add_border(bounds, border, 1.0F, 8.0F);
    ctx.add_text({bounds.x + 12.0F, bounds.y + 11.0F},
                 symbol_ + "  •  1m  •  SIMULATED",
                 muted,
                 chart_font(12.0F, nk::FontWeight::Medium));

    const auto plot = plot_rect(bounds);
    if (candles_.empty() || plot.width <= 0.0F || plot.height <= 0.0F) {
        ctx.add_text({plot.x + 12.0F, plot.y + 24.0F}, "No market data", muted, chart_font(13.0F));
        return;
    }

    const std::size_t first = first_visible_index();
    const std::size_t count = static_cast<std::size_t>(visible_candle_count());
    Price minimum = candles_[first].low;
    Price maximum = candles_[first].high;
    std::uint64_t maximum_volume = 1;
    for (std::size_t index = first; index < first + count; ++index) {
        minimum = std::min(minimum, candles_[index].low);
        maximum = std::max(maximum, candles_[index].high);
        maximum_volume = std::max(maximum_volume, candles_[index].volume);
    }
    const Price padding = std::max<Price>(1, (maximum - minimum) / 20);
    minimum -= padding;
    maximum += padding;
    const auto price_range = static_cast<double>(std::max<Price>(1, maximum - minimum));
    const auto price_y = [&](Price price) {
        const double fraction = static_cast<double>(maximum - price) / price_range;
        return plot.y + (static_cast<float>(fraction) * plot.height);
    };

    for (int line = 0; line <= 4; ++line) {
        const float ratio = static_cast<float>(line) / 4.0F;
        const float y = plot.y + (ratio * plot.height);
        const Price price = maximum - static_cast<Price>(ratio * price_range);
        ctx.add_line({plot.x, y}, {plot.right(), y}, grid);
        ctx.add_text({plot.right() + 7.0F, y - 12.0F}, format_price(price), muted, chart_font());
    }

    const float step = plot.width / static_cast<float>(count);
    const float body_width = std::max(2.0F, step * 0.62F);
    ctx.push_rounded_clip(plot, 0.0F);
    for (std::size_t offset = 0; offset < count; ++offset) {
        const auto& candle = candles_[first + offset];
        const float center_x = plot.x + ((static_cast<float>(offset) + 0.5F) * step);
        const auto color = candle.close >= candle.open ? rising : falling;
        ctx.add_line(
            {center_x, price_y(candle.high)}, {center_x, price_y(candle.low)}, color, 1.2F);
        const float open_y = price_y(candle.open);
        const float close_y = price_y(candle.close);
        const float body_top = std::min(open_y, close_y);
        const float body_height = std::max(1.5F, std::abs(close_y - open_y));
        ctx.add_color_rect({center_x - (body_width * 0.5F), body_top, body_width, body_height},
                           color);

        const float volume_height =
            (static_cast<float>(candle.volume) / static_cast<float>(maximum_volume)) *
            (plot.height * 0.16F);
        ctx.add_color_rect({center_x - (body_width * 0.5F),
                            plot.bottom() - volume_height,
                            body_width,
                            volume_height},
                           nk::Color{color.r, color.g, color.b, 0.22F});
    }

    const auto& latest = candles_.back();
    const float last_y = price_y(latest.close);
    ctx.add_line(
        {plot.x, last_y}, {plot.right(), last_y}, nk::Color{accent.r, accent.g, accent.b, 0.65F});

    if (cursor_index_ && *cursor_index_ >= first && *cursor_index_ < first + count) {
        const auto offset = *cursor_index_ - first;
        const auto& candle = candles_[*cursor_index_];
        const float center_x = plot.x + ((static_cast<float>(offset) + 0.5F) * step);
        ctx.add_line({center_x, plot.y}, {center_x, plot.bottom()}, accent);
        ctx.add_line({plot.x, price_y(candle.close)},
                     {plot.right(), price_y(candle.close)},
                     nk::Color{accent.r, accent.g, accent.b, 0.75F});
    }
    ctx.pop_container();

    const auto& inspected = candles_[cursor_index_.value_or(candles_.size() - 1)];
    const std::string summary =
        "O " + format_price(inspected.open) + "   H " + format_price(inspected.high) + "   L " +
        format_price(inspected.low) + "   C " + format_price(inspected.close) + "   Vol " +
        std::to_string(inspected.volume);
    ctx.add_text({bounds.x + 12.0F, bounds.bottom() - 24.0F}, summary, text, chart_font(11.5F));
}

void CandlestickChart::set_cursor(std::optional<std::size_t> index) {
    if (index && *index >= candles_.size()) {
        return;
    }
    if (cursor_index_ == index) {
        return;
    }
    cursor_index_ = index;
    update_accessible_summary();
    queue_canvas_redraw();
}

void CandlestickChart::update_accessible_summary() {
    auto& accessible = ensure_accessible();
    accessible.set_name(symbol_ + " candlestick chart");
    if (candles_.empty()) {
        accessible.set_value("No market data");
        return;
    }
    const auto index = cursor_index_.value_or(candles_.size() - 1);
    const auto& candle = candles_[index];
    const std::string prefix =
        cursor_index_ ? "Candle " + std::to_string(index + 1) : "Latest candle";
    accessible.set_value(prefix + ". Open " + format_price(candle.open) + ", high " +
                         format_price(candle.high) + ", low " + format_price(candle.low) +
                         ", close " + format_price(candle.close) + ", volume " +
                         std::to_string(candle.volume));
}

nk::Rect CandlestickChart::plot_rect(nk::Rect bounds) const {
    return {
        bounds.x + 12.0F,
        bounds.y + 36.0F,
        std::max(0.0F, bounds.width - 82.0F),
        std::max(0.0F, bounds.height - 70.0F),
    };
}

std::size_t CandlestickChart::first_visible_index() const {
    return candles_.size() - static_cast<std::size_t>(visible_candle_count());
}

} // namespace trading
