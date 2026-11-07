#pragma once

#include "trading_model.h"

#include <cstddef>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/widgets/canvas_widget.h>
#include <optional>
#include <string>
#include <vector>

namespace trading {

class CandlestickChart final : public nk::CanvasWidget {
public:
    [[nodiscard]] static std::shared_ptr<CandlestickChart> create();
    ~CandlestickChart() override;

    void set_data(std::string symbol, std::vector<Candle> candles);
    void zoom_in();
    void zoom_out();
    void reset_view();

    [[nodiscard]] int visible_candle_count() const;
    [[nodiscard]] std::optional<std::size_t> cursor_index() const;

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override;
    bool handle_mouse_event(const nk::MouseEvent& event) override;
    bool handle_key_event(const nk::KeyEvent& event) override;

private:
    CandlestickChart();

    void draw(nk::SnapshotContext& ctx, nk::Rect bounds);
    void set_cursor(std::optional<std::size_t> index);
    void update_accessible_summary();
    [[nodiscard]] nk::Rect plot_rect(nk::Rect bounds) const;
    [[nodiscard]] std::size_t first_visible_index() const;

    std::string symbol_;
    std::vector<Candle> candles_;
    int visible_count_ = 48;
    std::optional<std::size_t> cursor_index_;
    nk::ScopedConnection draw_connection_;
};

} // namespace trading
