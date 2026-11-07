#include "trading_terminal_app.h"

#include "instrument_chart_panel.h"
#include "order_ticket.h"
#include "orders_view.h"
#include "positions_view.h"
#include "trading_model.h"
#include "watchlist_view.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <nk/foundation/types.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/platform/window_inspector.h>
#include <nk/runtime/event_loop.h>
#include <nk/widgets/box.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/label.h>
#include <nk/widgets/split_view.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/tab_bar.h>
#include <nk/widgets/toast_overlay.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using trading::OrderType;
using trading::TradingModel;

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
        window_.set_child(nullptr);
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
        auto root = nk::Box::create(nk::Orientation::Vertical, 10.0F);
        root->set_padding(nk::Insets::uniform(12.0F));

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

        watchlist_ = std::make_unique<trading::WatchlistView>(model_);
        chart_panel_ = std::make_unique<trading::InstrumentChartPanel>();
        ticket_ = std::make_unique<trading::OrderTicket>();
        orders_ = std::make_unique<trading::OrdersView>(model_);
        positions_ = std::make_unique<trading::PositionsView>(model_);
        auto activity = build_activity_tabs();

        auto center_right = nk::SplitView::create(nk::Orientation::Horizontal);
        center_right->set_start_child(chart_panel_->widget());
        center_right->set_end_child(ticket_->widget());
        center_right->set_position(0.70F);
        center_right->set_min_start_size(460.0F);
        center_right->set_min_end_size(280.0F);

        auto workspace = nk::SplitView::create(nk::Orientation::Horizontal);
        workspace->set_start_child(watchlist_->widget());
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
        connections_.emplace_back(ticket_->on_submit_requested().connect(
            [this](const trading::OrderRequest& request) { (void)submit_order(request); }));
        connections_.emplace_back(watchlist_->on_instrument_selected().connect(
            [this](std::size_t index) { select_instrument(index); }));
        connections_.emplace_back(
            orders_->on_cancel_requested().connect([this] { cancel_selected_order(); }));
    }

    std::shared_ptr<nk::Widget> build_activity_tabs() {
        auto tabs = nk::TabBar::create();
        tabs->append_tab("Orders");
        tabs->set_tab_page(0, orders_->widget());
        tabs->append_tab("Positions");
        tabs->set_tab_page(1, positions_->widget());
        return tabs;
    }

    void select_instrument(std::size_t index) {
        if (!model_->select_instrument(index)) {
            return;
        }
        syncing_selection_ = true;
        symbol_selector_->set_selected_index(static_cast<int>(index));
        watchlist_->set_selected_instrument(index);
        syncing_selection_ = false;
        refresh_selected_instrument(true);
    }

    void refresh_all() {
        refresh_selected_instrument(true);
        refresh_market();
    }

    void refresh_market() {
        watchlist_->refresh();
        orders_->refresh();
        positions_->refresh();
        refresh_selected_instrument(false);
    }

    void refresh_selected_instrument(bool reset_limit_price) {
        const auto* instrument = model_->selected_instrument();
        if (instrument == nullptr) {
            return;
        }
        chart_panel_->set_instrument(*instrument);
        ticket_->set_instrument(*instrument, reset_limit_price);
    }

    std::uint64_t submit_order(const trading::OrderRequest& request) {
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
        const auto id = orders_->selected_order_id();
        if (!id) {
            show_feedback("Select an order to cancel.", true);
            return;
        }
        cancel_order_by_id(*id);
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
        ticket_->set_feedback(message);
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
        const std::uint64_t filled_order = submit_order(ticket_->request());
        ticket_->set_order_type(OrderType::Limit);
        const auto* instrument = model_->selected_instrument();
        ticket_->set_limit_price(instrument->ask - 200);
        const std::uint64_t working_order = submit_order(ticket_->request());
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
    std::shared_ptr<nk::ComboBox> symbol_selector_;
    std::unique_ptr<trading::WatchlistView> watchlist_;
    std::unique_ptr<trading::InstrumentChartPanel> chart_panel_;
    std::unique_ptr<trading::OrderTicket> ticket_;
    std::unique_ptr<trading::OrdersView> orders_;
    std::unique_ptr<trading::PositionsView> positions_;
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
