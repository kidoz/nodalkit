#include "trading_terminal_app.h"

#include "candlestick_chart.h"
#include "trading_model.h"

#include <algorithm>
#include <any>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <nk/accessibility/accessible.h>
#include <nk/accessibility/role.h>
#include <nk/foundation/types.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/platform/window_inspector.h>
#include <nk/runtime/event_loop.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/data_table.h>
#include <nk/widgets/label.h>
#include <nk/widgets/segmented_control.h>
#include <nk/widgets/spin_button.h>
#include <nk/widgets/split_view.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/tab_bar.h>
#include <nk/widgets/toast_overlay.h>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using trading::Order;
using trading::OrderSide;
using trading::OrderStatus;
using trading::OrderType;
using trading::Position;
using trading::Price;
using trading::TradingModel;

class ViewListModel final : public nk::AbstractListModel {
public:
    using RowCount = std::function<std::size_t()>;
    using RowText = std::function<std::string(std::size_t)>;

    ViewListModel(RowCount row_count, RowText row_text)
        : row_count_(std::move(row_count)), row_text_(std::move(row_text)) {}

    [[nodiscard]] std::size_t row_count() const override { return row_count_(); }

    [[nodiscard]] std::any data(std::size_t row) const override { return row; }

    [[nodiscard]] std::string display_text(std::size_t row) const override {
        return row_text_(row);
    }

    void rows_changed() { notify_model_reset(); }

    void data_changed() {
        if (row_count() > 0) {
            notify_data_changed(0, row_count() - 1);
        }
    }

private:
    RowCount row_count_;
    RowText row_text_;
};

std::string format_percent(Price current, Price previous) {
    if (previous == 0) {
        return "0.00%";
    }
    const double change =
        (static_cast<double>(current - previous) * 100.0) / static_cast<double>(previous);
    std::ostringstream stream;
    stream << (change >= 0.0 ? "+" : "") << std::fixed << std::setprecision(2) << change << '%';
    return stream.str();
}

std::string format_volume(std::uint64_t volume) {
    std::ostringstream stream;
    if (volume >= 1000000) {
        stream << std::fixed << std::setprecision(2) << static_cast<double>(volume) / 1000000.0
               << "M";
    } else if (volume >= 1000) {
        stream << std::fixed << std::setprecision(1) << static_cast<double>(volume) / 1000.0 << "K";
    } else {
        stream << volume;
    }
    return stream.str();
}

std::shared_ptr<nk::Box> make_panel(std::string_view title, std::shared_ptr<nk::Widget> child) {
    auto panel = nk::Box::create(nk::Orientation::Vertical, 7.0F);
    panel->append(nk::Label::create(std::string(title)));
    child->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    child->set_vertical_size_policy(nk::SizePolicy::Expanding);
    child->set_vertical_stretch(1);
    panel->append(std::move(child));
    return panel;
}

std::optional<std::filesystem::path> screenshot_directory(int argc, char** argv) {
    constexpr std::string_view Prefix = "--screenshot-dir=";
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index] != nullptr ? argv[index] : "";
        if (argument.starts_with(Prefix)) {
            return std::filesystem::path(argument.substr(Prefix.size()));
        }
    }
    return std::nullopt;
}

class TradingTerminalUI {
public:
    TradingTerminalUI(nk::Application& app, std::optional<std::filesystem::path> screenshot_dir)
        : app_(app)
        , window_({
              .title = "Nodal Trade — Simulation",
              .width = 1440,
              .height = 900,
          })
        , model_(std::make_shared<TradingModel>())
        , screenshot_dir_(std::move(screenshot_dir)) {
        build_ui();
    }

    ~TradingTerminalUI() {
        if (market_timer_.valid()) {
            app_.event_loop().cancel(market_timer_);
        }
    }

    int run() {
        window_.present();
        refresh_all();
        market_timer_ = app_.event_loop().set_interval(
            std::chrono::milliseconds(250),
            [this] {
                model_->advance_market();
                refresh_market();
            },
            "trading-terminal-market-tick");
        if (screenshot_dir_) {
            (void)app_.event_loop().set_timeout(std::chrono::milliseconds(100),
                                                [this] { run_scripted_capture(); });
        }
        return app_.run();
    }

private:
    void build_ui() {
        instrument_rows_ = std::make_shared<ViewListModel>(
            [this] { return model_->instruments().size(); },
            [this](std::size_t row) { return model_->instruments()[row].symbol; });
        order_rows_ = std::make_shared<ViewListModel>(
            [this] { return model_->orders().size(); },
            [this](std::size_t row) { return std::to_string(model_->orders()[row].id); });
        position_rows_ = std::make_shared<ViewListModel>(
            [this] { return model_->positions().size(); },
            [this](std::size_t row) { return model_->positions()[row].symbol; });

        auto root = nk::Box::create(nk::Orientation::Vertical, 10.0F);
        root->set_margin(nk::Insets::uniform(12.0F));

        auto top_bar = nk::Box::create(nk::Orientation::Horizontal, 10.0F);
        top_bar->append(nk::Label::create("Nodal Trade"));
        auto simulation = nk::Label::create("SIMULATION");
        simulation->set_tooltip("Deterministic simulated market data. No real orders are placed.");
        top_bar->append(simulation);

        symbol_selector_ = nk::ComboBox::create();
        std::vector<std::string> symbols;
        for (const auto& instrument : model_->instruments()) {
            symbols.push_back(instrument.symbol + "  •  " + instrument.name);
        }
        symbol_selector_->set_items(std::move(symbols));
        symbol_selector_->set_selected_index(0);
        symbol_selector_->set_horizontal_stretch(1);
        top_bar->append(symbol_selector_);

        auto account_selector = nk::ComboBox::create();
        account_selector->set_items({"Demo account  •  USD"});
        account_selector->set_selected_index(0);
        top_bar->append(account_selector);
        root->append(top_bar);

        auto watchlist = build_watchlist();
        auto chart = build_chart_panel();
        auto ticket = build_order_ticket();
        auto activity = build_activity_tabs();

        auto center_right = nk::SplitView::create(nk::Orientation::Horizontal);
        center_right->set_start_child(chart);
        center_right->set_end_child(ticket);
        center_right->set_position(0.70F);
        center_right->set_min_start_size(460.0F);
        center_right->set_min_end_size(280.0F);

        auto workspace = nk::SplitView::create(nk::Orientation::Horizontal);
        workspace->set_start_child(watchlist);
        workspace->set_end_child(center_right);
        workspace->set_position(0.29F);
        workspace->set_min_start_size(330.0F);
        workspace->set_min_end_size(740.0F);

        auto main_split = nk::SplitView::create(nk::Orientation::Vertical);
        main_split->set_start_child(workspace);
        main_split->set_end_child(activity);
        main_split->set_position(0.68F);
        main_split->set_min_start_size(390.0F);
        main_split->set_min_end_size(180.0F);
        main_split->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        main_split->set_vertical_size_policy(nk::SizePolicy::Expanding);
        main_split->set_vertical_stretch(1);
        root->append(main_split);

        status_bar_ = nk::StatusBar::create();
        status_bar_->set_segments({"SIMULATED DATA", "Feed connected", "Gateway connected"});
        status_bar_->set_trailing("No real orders");
        root->append(status_bar_);

        toasts_ = nk::ToastOverlay::create();
        toasts_->set_child(root);
        window_.set_child(toasts_);

        connections_.emplace_back(
            symbol_selector_->on_selection_changed().connect([this](int index) {
                if (!syncing_selection_ && index >= 0) {
                    select_instrument(static_cast<std::size_t>(index));
                }
            }));
        connections_.emplace_back(
            submit_button_->on_clicked().connect([this] { (void)submit_order_from_ticket(); }));
    }

    std::shared_ptr<nk::Widget> build_watchlist() {
        watchlist_table_ = nk::DataTable::create();
        watchlist_table_->set_model(instrument_rows_);
        watchlist_selection_ = std::make_shared<nk::SelectionModel>();
        watchlist_table_->set_selection_model(watchlist_selection_);
        watchlist_table_->set_row_height(29.0F);
        watchlist_table_->set_columns({
            {.id = "symbol",
             .title = "Symbol",
             .width = 72.0F,
             .text = [this](const nk::AbstractListModel&,
                            std::size_t row) { return model_->instruments()[row].symbol; },
             .expand = 1.0F},
            {.id = "last",
             .title = "Last",
             .width = 82.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     return trading::format_price(model_->instruments()[row].last);
                 }},
            {.id = "change",
             .title = "Change",
             .width = 76.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     const auto& item = model_->instruments()[row];
                     return format_percent(item.last, item.previous_close);
                 }},
            {.id = "bid",
             .title = "Bid",
             .width = 80.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     return trading::format_price(model_->instruments()[row].bid);
                 }},
            {.id = "ask",
             .title = "Ask",
             .width = 80.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     return trading::format_price(model_->instruments()[row].ask);
                 }},
        });
        watchlist_selection_->set_current_row(0);
        watchlist_selection_->select(0);
        connections_.emplace_back(
            watchlist_selection_->on_current_changed().connect([this](std::size_t row) {
                if (!syncing_selection_ && row < model_->instruments().size()) {
                    select_instrument(row);
                }
            }));
        return make_panel("Watchlist  •  SIM", watchlist_table_);
    }

    std::shared_ptr<nk::Widget> build_chart_panel() {
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
        return panel;
    }

    std::shared_ptr<nk::Widget> build_order_ticket() {
        auto panel = nk::Box::create(nk::Orientation::Vertical, 7.0F);
        panel->append(nk::Label::create("Order ticket"));
        ticket_instrument_ = nk::Label::create();
        panel->append(ticket_instrument_);

        panel->append(nk::Label::create("Side"));
        side_selector_ = nk::SegmentedControl::create();
        side_selector_->set_segments({"Buy", "Sell"});
        side_selector_->set_selected_index(0);
        panel->append(side_selector_);

        panel->append(nk::Label::create("Order type"));
        type_selector_ = nk::ComboBox::create();
        type_selector_->set_items({"Market", "Limit"});
        type_selector_->set_selected_index(0);
        panel->append(type_selector_);

        panel->append(nk::Label::create("Quantity"));
        quantity_input_ = nk::SpinButton::create();
        quantity_input_->set_range(1.0, 1000000.0);
        quantity_input_->set_step(1.0);
        quantity_input_->set_precision(0);
        quantity_input_->set_value(10.0);
        panel->append(quantity_input_);

        price_label_ = nk::Label::create("Limit price  •  USD");
        panel->append(price_label_);
        price_input_ = nk::SpinButton::create();
        price_input_->set_range(0.01, 1000000.0);
        price_input_->set_step(0.01);
        price_input_->set_precision(2);
        price_input_->set_sensitive(false);
        panel->append(price_input_);

        notional_label_ = nk::Label::create();
        notional_label_->set_wrapping(true);
        panel->append(notional_label_);

        submit_button_ = nk::Button::create();
        panel->append(submit_button_);

        feedback_ = nk::Label::create("Ready. Orders remain inside this simulation.");
        feedback_->set_wrapping(true);
        feedback_->ensure_accessible().set_role(nk::AccessibleRole::Status);
        feedback_->ensure_accessible().set_name("Order entry status");
        panel->append(feedback_);

        connections_.emplace_back(side_selector_->on_selection_changed().connect(
            [this](int) { update_ticket_controls(); }));
        connections_.emplace_back(type_selector_->on_selection_changed().connect(
            [this](int) { update_ticket_controls(); }));
        connections_.emplace_back(
            quantity_input_->on_value_changed().connect([this](double) { update_notional(); }));
        connections_.emplace_back(
            price_input_->on_value_changed().connect([this](double) { update_notional(); }));
        update_ticket_controls();
        return panel;
    }

    std::shared_ptr<nk::Widget> build_activity_tabs() {
        auto orders_page = nk::Box::create(nk::Orientation::Vertical, 7.0F);
        auto orders_toolbar = nk::Box::create(nk::Orientation::Horizontal, 10.0F);
        orders_toolbar->append(nk::Label::create("Working and completed orders"));
        cancel_button_ = nk::Button::create("Cancel selected working order");
        orders_toolbar->append(cancel_button_);
        orders_page->append(orders_toolbar);

        orders_table_ = nk::DataTable::create();
        orders_table_->set_model(order_rows_);
        order_selection_ = std::make_shared<nk::SelectionModel>();
        orders_table_->set_selection_model(order_selection_);
        orders_table_->set_columns({
            {.id = "id",
             .title = "ID",
             .width = 58.0F,
             .text = [this](const nk::AbstractListModel&,
                            std::size_t row) { return std::to_string(model_->orders()[row].id); }},
            {.id = "symbol",
             .title = "Symbol",
             .width = 72.0F,
             .text = [this](const nk::AbstractListModel&,
                            std::size_t row) { return model_->orders()[row].symbol; }},
            {.id = "side",
             .title = "Side",
             .width = 58.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     return std::string(trading::order_side_name(model_->orders()[row].side));
                 }},
            {.id = "type",
             .title = "Type",
             .width = 66.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     return std::string(trading::order_type_name(model_->orders()[row].type));
                 }},
            {.id = "quantity",
             .title = "Qty",
             .width = 64.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     return std::to_string(model_->orders()[row].quantity);
                 }},
            {.id = "price",
             .title = "Limit",
             .width = 86.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     const auto& order = model_->orders()[row];
                     return order.type == OrderType::Limit
                                ? trading::format_price(order.limit_price)
                                : std::string("—");
                 }},
            {.id = "fill",
             .title = "Avg fill",
             .width = 88.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     const auto& order = model_->orders()[row];
                     return order.status == OrderStatus::Filled
                                ? trading::format_price(order.average_fill_price)
                                : std::string("—");
                 }},
            {.id = "status",
             .title = "Status",
             .width = 92.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     return std::string(trading::order_status_name(model_->orders()[row].status));
                 },
             .expand = 1.0F},
        });
        orders_table_->set_vertical_stretch(1);
        orders_table_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        orders_table_->set_vertical_size_policy(nk::SizePolicy::Expanding);
        orders_page->append(orders_table_);

        auto positions_page = nk::Box::create(nk::Orientation::Vertical, 7.0F);
        account_summary_ = nk::Label::create("Demo account  •  USD  •  Simulated P&L");
        positions_page->append(account_summary_);
        positions_table_ = nk::DataTable::create();
        positions_table_->set_model(position_rows_);
        positions_table_->set_columns({
            {.id = "symbol",
             .title = "Symbol",
             .width = 100.0F,
             .text = [this](const nk::AbstractListModel&,
                            std::size_t row) { return model_->positions()[row].symbol; },
             .expand = 1.0F},
            {.id = "quantity",
             .title = "Quantity",
             .width = 110.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     return std::to_string(model_->positions()[row].quantity);
                 }},
            {.id = "average",
             .title = "Average price",
             .width = 130.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     return trading::format_price(model_->positions()[row].average_price);
                 }},
            {.id = "last",
             .title = "Last",
             .width = 120.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     const auto& position = model_->positions()[row];
                     const auto* instrument = model_->find_instrument(position.symbol);
                     return instrument != nullptr ? trading::format_price(instrument->last)
                                                  : std::string("—");
                 }},
            {.id = "pnl",
             .title = "Unrealized P&L",
             .width = 150.0F,
             .text =
                 [this](const nk::AbstractListModel&, std::size_t row) {
                     const auto& position = model_->positions()[row];
                     const auto* instrument = model_->find_instrument(position.symbol);
                     if (instrument == nullptr) {
                         return std::string("—");
                     }
                     const Price pnl = (instrument->last - position.average_price) *
                                       static_cast<Price>(position.quantity);
                     return trading::format_signed_price(pnl);
                 }},
        });
        positions_table_->set_vertical_stretch(1);
        positions_table_->set_horizontal_size_policy(nk::SizePolicy::Expanding);
        positions_table_->set_vertical_size_policy(nk::SizePolicy::Expanding);
        positions_page->append(positions_table_);

        auto tabs = nk::TabBar::create();
        tabs->append_tab("Orders");
        tabs->set_tab_page(0, orders_page);
        tabs->append_tab("Positions");
        tabs->set_tab_page(1, positions_page);
        connections_.emplace_back(
            cancel_button_->on_clicked().connect([this] { cancel_selected_order(); }));
        return tabs;
    }

    void select_instrument(std::size_t index) {
        if (!model_->select_instrument(index)) {
            return;
        }
        syncing_selection_ = true;
        symbol_selector_->set_selected_index(static_cast<int>(index));
        watchlist_selection_->set_current_row(index);
        watchlist_selection_->select(index);
        syncing_selection_ = false;
        refresh_selected_instrument(true);
    }

    void refresh_all() {
        seen_order_revision_ = std::numeric_limits<std::uint64_t>::max();
        seen_position_revision_ = std::numeric_limits<std::uint64_t>::max();
        instrument_rows_->rows_changed();
        refresh_selected_instrument(true);
        refresh_market();
    }

    void refresh_market() {
        instrument_rows_->data_changed();
        if (seen_order_revision_ != model_->order_revision()) {
            order_rows_->rows_changed();
            seen_order_revision_ = model_->order_revision();
        }
        if (seen_position_revision_ != model_->position_revision()) {
            position_rows_->rows_changed();
            seen_position_revision_ = model_->position_revision();
        } else {
            position_rows_->data_changed();
        }
        refresh_selected_instrument(false);
    }

    void refresh_selected_instrument(bool reset_limit_price) {
        const auto* instrument = model_->selected_instrument();
        if (instrument == nullptr) {
            return;
        }
        instrument_name_->set_text(instrument->symbol + "  •  " + instrument->name);
        last_price_->set_text(trading::format_price(instrument->last));
        price_change_->set_text(
            trading::format_signed_price(instrument->last - instrument->previous_close) + "  (" +
            format_percent(instrument->last, instrument->previous_close) + ")");
        quote_summary_->set_text("Bid " + trading::format_price(instrument->bid) + "   Ask " +
                                 trading::format_price(instrument->ask) + "   Volume " +
                                 format_volume(instrument->volume));
        ticket_instrument_->set_text(instrument->symbol + "  •  " +
                                     trading::format_price(instrument->last));
        chart_->set_data(instrument->symbol, instrument->candles);
        if (reset_limit_price) {
            price_input_->set_value(static_cast<double>(instrument->ask) / 100.0);
        }
        update_notional();
    }

    void update_ticket_controls() {
        const bool limit = type_selector_->selected_index() == 1;
        price_input_->set_sensitive(limit);
        price_label_->set_text(limit ? "Limit price  •  USD"
                                     : "Limit price  •  not used for market orders");
        update_submit_label();
        update_notional();
    }

    void update_submit_label() {
        const auto* instrument = model_->selected_instrument();
        const std::string side = side_selector_->selected_index() == 1 ? "Sell" : "Buy";
        submit_button_->set_label(side + " " +
                                  (instrument != nullptr ? instrument->symbol : std::string()) +
                                  "  •  Simulated");
    }

    void update_notional() {
        if (notional_label_ == nullptr) {
            return;
        }
        const auto* instrument = model_->selected_instrument();
        if (instrument == nullptr) {
            return;
        }
        const Price unit_price =
            type_selector_->selected_index() == 1
                ? static_cast<Price>(std::llround(price_input_->value() * 100.0))
                : (side_selector_->selected_index() == 1 ? instrument->bid : instrument->ask);
        const auto quantity = static_cast<Price>(quantity_input_->value());
        notional_label_->set_text("Estimated notional: " +
                                  trading::format_price(unit_price * quantity));
        update_submit_label();
    }

    std::uint64_t submit_order_from_ticket() {
        const auto* instrument = model_->selected_instrument();
        if (instrument == nullptr) {
            return 0;
        }
        trading::OrderRequest request{
            .symbol = instrument->symbol,
            .side = side_selector_->selected_index() == 1 ? OrderSide::Sell : OrderSide::Buy,
            .type = type_selector_->selected_index() == 1 ? OrderType::Limit : OrderType::Market,
            .quantity = static_cast<int>(quantity_input_->value()),
            .limit_price = static_cast<Price>(std::llround(price_input_->value() * 100.0)),
        };
        auto result = model_->submit_order(request);
        if (!result) {
            show_feedback(result.error(), true);
            return 0;
        }

        const auto& order = model_->orders().back();
        const std::string message = "Order " + std::to_string(result.value()) + " " +
                                    std::string(trading::order_status_name(order.status)) +
                                    "  •  " + std::string(trading::order_side_name(order.side)) +
                                    " " + std::to_string(order.quantity) + " " + order.symbol;
        show_feedback(message, false);
        refresh_market();
        return result.value();
    }

    void cancel_selected_order() {
        const auto row = order_selection_->current_row();
        if (row >= model_->orders().size()) {
            show_feedback("Select an order to cancel.", true);
            return;
        }
        cancel_order_by_id(model_->orders()[row].id);
    }

    void cancel_order_by_id(std::uint64_t id) {
        if (!model_->cancel_order(id)) {
            show_feedback("Order " + std::to_string(id) + " is not a working order.", true);
            return;
        }
        show_feedback("Order " + std::to_string(id) + " cancelled in the simulation.", false);
        refresh_market();
    }

    void show_feedback(const std::string& message, bool error) {
        feedback_->set_text(message);
        toasts_->add_toast({
            .title = message,
            .action_label = {},
            .priority = error ? nk::ToastPriority::High : nk::ToastPriority::Normal,
            .timeout = std::chrono::milliseconds(5000),
        });
    }

    void save_screenshot(const std::filesystem::path& path) {
        (void)app_.event_loop().poll();
        (void)app_.event_loop().poll();
        auto result = window_.inspector().save_debug_screenshot_ppm_file(path.string());
        if (!result) {
            std::cerr << "trading_terminal: " << result.error() << '\n';
            screenshot_failed_ = true;
        }
    }

    void run_scripted_capture() {
        std::error_code error;
        std::filesystem::create_directories(*screenshot_dir_, error);
        if (error) {
            std::cerr << "trading_terminal: cannot create screenshot directory: " << error.message()
                      << '\n';
            app_.quit(1);
            return;
        }

        save_screenshot(*screenshot_dir_ / "terminal-initial.ppm");
        const std::uint64_t filled_order = submit_order_from_ticket();
        type_selector_->set_selected_index(1);
        const auto* instrument = model_->selected_instrument();
        price_input_->set_value(static_cast<double>(instrument->ask - 200) / 100.0);
        const std::uint64_t working_order = submit_order_from_ticket();
        cancel_order_by_id(working_order);
        save_screenshot(*screenshot_dir_ / "terminal-orders.ppm");

        if (filled_order == 0 || working_order == 0 || screenshot_failed_) {
            std::cerr << "trading_terminal smoke: FAILED\n";
            app_.quit(1);
            return;
        }
        std::cout << "trading_terminal smoke: filled order " << filled_order
                  << ", cancelled working order " << working_order << ", screenshots saved\n";
        app_.quit(0);
    }

    nk::Application& app_;
    nk::Window window_;
    std::shared_ptr<TradingModel> model_;
    std::optional<std::filesystem::path> screenshot_dir_;
    nk::CallbackHandle market_timer_;
    bool screenshot_failed_ = false;
    bool syncing_selection_ = false;
    std::uint64_t seen_order_revision_ = 0;
    std::uint64_t seen_position_revision_ = 0;

    std::shared_ptr<ViewListModel> instrument_rows_;
    std::shared_ptr<ViewListModel> order_rows_;
    std::shared_ptr<ViewListModel> position_rows_;
    std::shared_ptr<nk::SelectionModel> watchlist_selection_;
    std::shared_ptr<nk::SelectionModel> order_selection_;
    std::shared_ptr<nk::ComboBox> symbol_selector_;
    std::shared_ptr<nk::DataTable> watchlist_table_;
    std::shared_ptr<nk::DataTable> orders_table_;
    std::shared_ptr<nk::DataTable> positions_table_;
    std::shared_ptr<trading::CandlestickChart> chart_;
    std::shared_ptr<nk::Label> instrument_name_;
    std::shared_ptr<nk::Label> last_price_;
    std::shared_ptr<nk::Label> price_change_;
    std::shared_ptr<nk::Label> quote_summary_;
    std::shared_ptr<nk::Label> ticket_instrument_;
    std::shared_ptr<nk::SegmentedControl> side_selector_;
    std::shared_ptr<nk::ComboBox> type_selector_;
    std::shared_ptr<nk::SpinButton> quantity_input_;
    std::shared_ptr<nk::SpinButton> price_input_;
    std::shared_ptr<nk::Label> price_label_;
    std::shared_ptr<nk::Label> notional_label_;
    std::shared_ptr<nk::Button> submit_button_;
    std::shared_ptr<nk::Button> cancel_button_;
    std::shared_ptr<nk::Label> feedback_;
    std::shared_ptr<nk::Label> account_summary_;
    std::shared_ptr<nk::StatusBar> status_bar_;
    std::shared_ptr<nk::ToastOverlay> toasts_;
    std::vector<nk::ScopedConnection> connections_;
};

} // namespace

int run_trading_terminal(int argc, char** argv) {
    nk::Application app(argc, argv);
    TradingTerminalUI terminal(app, screenshot_directory(argc, argv));
    return terminal.run();
}
