#include "instrument_chart_panel.h"

#include "candlestick_chart.h"
#include "trading_model.h"

#include <nk/foundation/types.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>

namespace trading {
InstrumentChartPanel::InstrumentChartPanel() {
    auto panel = nk::Box::create(nk::Orientation::Vertical, 7.0F);
    instrument_name_ = nk::Label::create();
    last_price_ = nk::Label::create();
    price_change_ = nk::Label::create();
    quote_summary_ = nk::Label::create();

    auto instrument_row = nk::Box::create(nk::Orientation::Horizontal, 12.0F);
    instrument_row->append(instrument_name_);
    instrument_row->append(last_price_);
    instrument_row->append(price_change_);
    panel->append(instrument_row);
    panel->append(quote_summary_);

    chart_ = trading::CandlestickChart::create();
    chart_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    chart_->set_vertical_size_policy(nk::SizePolicy::Expanding);
    chart_->set_vertical_stretch(1);
    panel->append(chart_);

    auto controls = nk::Box::create(nk::Orientation::Horizontal, 7.0F);
    auto zoom_out = nk::Button::create("−");
    zoom_out->set_tooltip("Zoom out (Minus)");
    auto zoom_in = nk::Button::create("+");
    zoom_in->set_tooltip("Zoom in (Equals)");
    auto reset = nk::Button::create("Reset chart");
    controls->append(zoom_out);
    controls->append(zoom_in);
    controls->append(reset);
    panel->append(controls);

    connections_.emplace_back(zoom_out->on_clicked().connect([this] { chart_->zoom_out(); }));
    connections_.emplace_back(zoom_in->on_clicked().connect([this] { chart_->zoom_in(); }));
    connections_.emplace_back(reset->on_clicked().connect([this] { chart_->reset_view(); }));
    root_ = panel;
}

void InstrumentChartPanel::set_instrument(const Instrument& instrument) {
    instrument_name_->set_text(instrument.symbol + "  •  " + instrument.name);
    last_price_->set_text(trading::format_price(instrument.last));
    price_change_->set_text(
        trading::format_signed_price(instrument.last - instrument.previous_close) + "  (" +
        format_percent(instrument.last, instrument.previous_close) + ")");
    quote_summary_->set_text("Bid " + trading::format_price(instrument.bid) + "   Ask " +
                             trading::format_price(instrument.ask) + "   Volume " +
                             format_volume(instrument.volume));
    chart_->set_data(instrument.symbol, instrument.candles);
}
} // namespace trading
