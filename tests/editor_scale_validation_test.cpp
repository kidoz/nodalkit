/// @file editor_scale_validation_test.cpp
/// @brief Stress validation for editor-scale table, grid, and canvas usage.

#include <any>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/canvas_widget.h>
#include <nk/widgets/data_table.h>
#include <nk/widgets/grid_view.h>
#include <nk/widgets/scroll_area.h>
#include <string>

namespace {

class CountingListModel : public nk::AbstractListModel {
public:
    explicit CountingListModel(std::size_t rows) : rows_(rows) {}

    [[nodiscard]] std::size_t row_count() const override {
        ++row_count_queries_;
        return rows_;
    }

    [[nodiscard]] std::any data(std::size_t row) const override {
        ++data_queries_;
        return row;
    }

    [[nodiscard]] std::string display_text(std::size_t row) const override {
        ++display_text_queries_;
        return "row " + std::to_string(row);
    }

    [[nodiscard]] std::size_t row_count_queries() const { return row_count_queries_; }

    [[nodiscard]] std::size_t data_queries() const { return data_queries_; }

    [[nodiscard]] std::size_t display_text_queries() const { return display_text_queries_; }

    void reset_counters() const {
        row_count_queries_ = 0;
        data_queries_ = 0;
        display_text_queries_ = 0;
    }

private:
    std::size_t rows_ = 0;
    mutable std::size_t row_count_queries_ = 0;
    mutable std::size_t data_queries_ = 0;
    mutable std::size_t display_text_queries_ = 0;
};

class SnapshotDataTable : public nk::DataTable {
public:
    static std::shared_ptr<SnapshotDataTable> create() {
        return std::shared_ptr<SnapshotDataTable>(new SnapshotDataTable());
    }

    void snapshot_for_test(nk::SnapshotContext& ctx) const { snapshot(ctx); }

private:
    SnapshotDataTable() = default;
};

class SnapshotGridView : public nk::GridView {
public:
    static std::shared_ptr<SnapshotGridView> create() {
        return std::shared_ptr<SnapshotGridView>(new SnapshotGridView());
    }

    void snapshot_for_test(nk::SnapshotContext& ctx) const { snapshot(ctx); }

private:
    SnapshotGridView() = default;
};

class SnapshotCanvasWidget : public nk::CanvasWidget {
public:
    static std::shared_ptr<SnapshotCanvasWidget> create() {
        return std::shared_ptr<SnapshotCanvasWidget>(new SnapshotCanvasWidget());
    }

    void snapshot_for_test(nk::SnapshotContext& ctx) const { snapshot(ctx); }

private:
    SnapshotCanvasWidget() = default;
};

class SnapshotScrollArea : public nk::ScrollArea {
public:
    static std::shared_ptr<SnapshotScrollArea> create() {
        return std::shared_ptr<SnapshotScrollArea>(new SnapshotScrollArea());
    }

    void snapshot_for_test(nk::SnapshotContext& ctx) const { snapshot(ctx); }

private:
    SnapshotScrollArea() = default;
};

class EditorSurfaceWidget : public nk::Widget {
public:
    static std::shared_ptr<EditorSurfaceWidget> create(float width, float height) {
        return std::shared_ptr<EditorSurfaceWidget>(new EditorSurfaceWidget(width, height));
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        ++measure_queries_;
        return {width_, height_, width_, height_};
    }

    void queue_cell_damage(nk::Rect damage) { queue_redraw(damage); }

    [[nodiscard]] std::size_t measure_queries() const { return measure_queries_; }

    [[nodiscard]] std::size_t snapshot_calls() const { return snapshot_calls_; }

    void reset_counters() const {
        measure_queries_ = 0;
        snapshot_calls_ = 0;
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        ++snapshot_calls_;
        ctx.add_color_rect(allocation(), nk::Color::from_rgb(245, 247, 250));
    }

private:
    EditorSurfaceWidget(float width, float height) : width_(width), height_(height) {}

    float width_ = 0.0F;
    float height_ = 0.0F;
    mutable std::size_t measure_queries_ = 0;
    mutable std::size_t snapshot_calls_ = 0;
};

} // namespace

TEST_CASE("DataTable snapshots only visible rows for editor-scale models",
          "[editor-scale][table]") {
    static constexpr std::size_t kRows = 100'000;
    auto model = std::make_shared<CountingListModel>(kRows);
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    auto table = SnapshotDataTable::create();
    table->set_model(model);
    table->set_selection_model(selection);
    table->set_row_height(20.0F);
    table->set_header_height(24.0F);
    table->set_columns({
        nk::DataTableColumn{.id = "track", .title = "Track", .width = 96.0F},
        nk::DataTableColumn{
            .id = "step",
            .title = "Step",
            .width = 72.0F,
            .text =
                [](const nk::AbstractListModel& source, std::size_t row) {
                    return "step " + std::to_string(std::any_cast<std::size_t>(source.data(row)));
                },
        },
        nk::DataTableColumn{
            .id = "note",
            .title = "Note",
            .width = 72.0F,
            .text =
                [](const nk::AbstractListModel& source, std::size_t row) {
                    return "note " + std::to_string(std::any_cast<std::size_t>(source.data(row)));
                },
        },
        nk::DataTableColumn{
            .id = "velocity",
            .title = "Velocity",
            .width = 96.0F,
            .text =
                [](const nk::AbstractListModel& source, std::size_t row) {
                    return "vel " + std::to_string(std::any_cast<std::size_t>(source.data(row)));
                },
        },
    });
    table->allocate({0.0F, 0.0F, 420.0F, 164.0F});

    model->reset_counters();
    nk::SnapshotContext ctx;
    table->snapshot_for_test(ctx);

    REQUIRE(model->display_text_queries() <= 8);
    REQUIRE(model->data_queries() <= 24);
    REQUIRE(model->display_text_queries() + model->data_queries() < 40);

    model->reset_counters();
    REQUIRE(table->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::End}));
    REQUIRE(selection->current_row() == kRows - 1);
    REQUIRE(model->display_text_queries() <= 8);
    REQUIRE(model->data_queries() <= 12);
}

TEST_CASE("GridView snapshots only visible items for editor-scale models", "[editor-scale][grid]") {
    static constexpr std::size_t kItems = 100'000;
    auto model = std::make_shared<CountingListModel>(kItems);
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    auto grid = SnapshotGridView::create();
    grid->set_model(model);
    grid->set_selection_model(selection);
    grid->set_cell_width(72.0F);
    grid->set_cell_height(40.0F);
    grid->set_gap(4.0F);
    grid->allocate({0.0F, 0.0F, 340.0F, 144.0F});

    model->reset_counters();
    nk::SnapshotContext ctx;
    grid->snapshot_for_test(ctx);

    REQUIRE(model->display_text_queries() <= 16);
    REQUIRE(model->data_queries() == 0);

    model->reset_counters();
    REQUIRE(grid->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::End}));
    REQUIRE(selection->current_row() == kItems - 1);
    REQUIRE(model->display_text_queries() <= 8);
    REQUIRE(model->data_queries() == 0);
}

TEST_CASE("CanvasWidget supports custom snapshots and explicit sub-rect damage",
          "[editor-scale][canvas]") {
    auto canvas = SnapshotCanvasWidget::create();
    canvas->allocate({12.0F, 18.0F, 640.0F, 360.0F});

    std::size_t draw_calls = 0;
    nk::Rect observed_rect{};
    auto draw_connection = canvas->on_draw().connect([&](nk::SnapshotContext& ctx, nk::Rect rect) {
        ++draw_calls;
        observed_rect = rect;
        ctx.add_color_rect({rect.x + 4.0F, rect.y + 8.0F, 24.0F, 12.0F},
                           nk::Color::from_rgb(30, 120, 180));
    });
    (void)draw_connection;

    nk::SnapshotContext ctx;
    canvas->snapshot_for_test(ctx);

    REQUIRE(draw_calls == 1);
    REQUIRE(observed_rect.x == 12.0F);
    REQUIRE(observed_rect.y == 18.0F);
    REQUIRE(observed_rect.width == 640.0F);
    REQUIRE(observed_rect.height == 360.0F);

    auto localized_canvas = SnapshotCanvasWidget::create();
    localized_canvas->allocate({12.0F, 18.0F, 640.0F, 360.0F});
    localized_canvas->queue_redraw({40.0F, 32.0F, 18.0F, 14.0F});
    const auto localized_damage = localized_canvas->debug_damage_regions();
    REQUIRE(localized_damage.size() == 1);
    REQUIRE(localized_damage.front().x == 52.0F);
    REQUIRE(localized_damage.front().y == 50.0F);
    REQUIRE(localized_damage.front().width == 18.0F);
    REQUIRE(localized_damage.front().height == 14.0F);

    auto full_canvas = SnapshotCanvasWidget::create();
    full_canvas->allocate({12.0F, 18.0F, 640.0F, 360.0F});
    full_canvas->queue_canvas_redraw();
    const auto full_canvas_damage = full_canvas->debug_damage_regions();
    REQUIRE(full_canvas_damage.size() == 1);
    REQUIRE(full_canvas_damage.front().x == 12.0F);
    REQUIRE(full_canvas_damage.front().y == 18.0F);
    REQUIRE(full_canvas_damage.front().width == 640.0F);
    REQUIRE(full_canvas_damage.front().height == 360.0F);
}

TEST_CASE("ScrollArea composes a stable editor surface with localized content damage",
          "[editor-scale][scroll]") {
    auto scroll_area = SnapshotScrollArea::create();
    scroll_area->set_h_scroll_policy(nk::ScrollPolicy::Automatic);
    scroll_area->set_v_scroll_policy(nk::ScrollPolicy::Automatic);

    auto surface = EditorSurfaceWidget::create(4096.0F, 120000.0F);
    scroll_area->set_content(surface);
    scroll_area->allocate({0.0F, 0.0F, 320.0F, 180.0F});

    scroll_area->scroll_to(1024.0F, 64000.0F);
    scroll_area->allocate({0.0F, 0.0F, 320.0F, 180.0F});

    REQUIRE(scroll_area->h_offset() == 1024.0F);
    REQUIRE(scroll_area->v_offset() == 64000.0F);
    REQUIRE(surface->allocation().x == -1024.0F);
    REQUIRE(surface->allocation().y == -64000.0F);
    REQUIRE(surface->allocation().width == 4096.0F);
    REQUIRE(surface->allocation().height == 120000.0F);

    surface->reset_counters();
    nk::SnapshotContext ctx;
    scroll_area->snapshot_for_test(ctx);

    REQUIRE(surface->snapshot_calls() == 1);
    REQUIRE(surface->measure_queries() <= 8);

    surface->queue_cell_damage({1032.0F, 64012.0F, 16.0F, 12.0F});
    const auto damage = surface->debug_damage_regions();
    REQUIRE(damage.size() == 1);
    REQUIRE(damage.front().x == 8.0F);
    REQUIRE(damage.front().y == 12.0F);
    REQUIRE(damage.front().width == 16.0F);
    REQUIRE(damage.front().height == 12.0F);
}
