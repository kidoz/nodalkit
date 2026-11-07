#pragma once

#include "candlestick_chart.h"
#include "trading_model.h"

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/widgets/box.h>
#include <nk/widgets/label.h>
#include <vector>

namespace trading {

class InstrumentChartPanel {
public:
    InstrumentChartPanel();

    InstrumentChartPanel(const InstrumentChartPanel&) = delete;
    InstrumentChartPanel& operator=(const InstrumentChartPanel&) = delete;

    [[nodiscard]] std::shared_ptr<nk::Widget> widget() const { return root_; }

    void set_instrument(const Instrument& instrument);

private:
    std::shared_ptr<nk::Box> root_;
    std::shared_ptr<nk::Label> instrument_name_;
    std::shared_ptr<nk::Label> last_price_;
    std::shared_ptr<nk::Label> price_change_;
    std::shared_ptr<nk::Label> quote_summary_;
    std::shared_ptr<CandlestickChart> chart_;
    std::vector<nk::ScopedConnection> connections_;
};
} // namespace trading
