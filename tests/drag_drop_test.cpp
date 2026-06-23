/// @file drag_drop_test.cpp
/// @brief Tests for public drag-and-drop payload dispatch.

#include <any>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <nk/platform/drag_drop.h>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/ui_core/widget.h>

namespace {

class TestContainer : public nk::Widget {
public:
    static std::shared_ptr<TestContainer> create() {
        return std::shared_ptr<TestContainer>(new TestContainer());
    }

    void append(std::shared_ptr<nk::Widget> child) { append_child(std::move(child)); }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override { nk::Widget::snapshot(ctx); }

private:
    TestContainer() = default;
};

class DropProbeWidget : public nk::Widget {
public:
    static std::shared_ptr<DropProbeWidget> create() {
        return std::shared_ptr<DropProbeWidget>(new DropProbeWidget());
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {80.0F, 40.0F, 120.0F, 80.0F};
    }

protected:
    void snapshot(nk::SnapshotContext& /*ctx*/) const override {}

private:
    DropProbeWidget() = default;
};

} // namespace

TEST_CASE("Window dispatches in-process drag payloads to widget drop targets", "[drag-drop]") {
    nk::Window window({.title = "Internal drag", .width = 240, .height = 100});
    auto root = TestContainer::create();
    auto left = DropProbeWidget::create();
    auto right = DropProbeWidget::create();
    root->append(left);
    root->append(right);
    window.set_child(root);

    root->allocate({0.0F, 0.0F, 240.0F, 100.0F});
    left->allocate({0.0F, 0.0F, 120.0F, 100.0F});
    right->allocate({120.0F, 0.0F, 120.0F, 100.0F});

    int left_enters = 0;
    int left_leaves = 0;
    int right_enters = 0;
    int right_motions = 0;
    int right_drops = 0;
    nk::DragOperation accepted = nk::DragOperation::None;

    auto left_enter_conn = left->on_drag_enter().connect([&](nk::DragDropEvent& event) {
        REQUIRE(event.payload != nullptr);
        REQUIRE(event.payload->mime_type == "application/x-nodalkit-test-row");
        ++left_enters;
    });
    auto left_leave_conn = left->on_drag_leave().connect([&](nk::DragDropEvent& event) {
        REQUIRE(event.payload != nullptr);
        ++left_leaves;
    });
    auto right_enter_conn = right->on_drag_enter().connect([&](nk::DragDropEvent& event) {
        REQUIRE(event.payload != nullptr);
        ++right_enters;
    });
    auto right_motion_conn = right->on_drag_motion().connect([&](nk::DragDropEvent& event) {
        REQUIRE(event.requested_operation == nk::DragOperation::Move);
        ++right_motions;
    });
    auto right_drop_conn = right->on_drop().connect([&](nk::DragDropEvent& event) {
        REQUIRE(event.payload != nullptr);
        REQUIRE(event.payload->has_application_data());
        REQUIRE(std::any_cast<int>(event.payload->application_data) == 7);
        event.accept(nk::DragOperation::Move);
        accepted = event.accepted_operation;
        ++right_drops;
    });
    (void)left_enter_conn;
    (void)left_leave_conn;
    (void)right_enter_conn;
    (void)right_motion_conn;
    (void)right_drop_conn;

    window.start_drag(nk::DragPayload::from_application_data("application/x-nodalkit-test-row", 7),
                      nk::DragOperation::Move);
    REQUIRE(window.is_drag_active());

    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::DragStart,
        .x = 40.0F,
        .y = 40.0F,
    });
    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::DragUpdate,
        .x = 160.0F,
        .y = 40.0F,
    });
    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::DragEnd,
        .x = 160.0F,
        .y = 40.0F,
    });

    REQUIRE_FALSE(window.is_drag_active());
    REQUIRE(left_enters == 1);
    REQUIRE(left_leaves == 1);
    REQUIRE(right_enters == 1);
    REQUIRE(right_motions == 1);
    REQUIRE(right_drops == 1);
    REQUIRE(accepted == nk::DragOperation::Move);
}

TEST_CASE("Window dispatches external file-drop payloads to widget drop targets", "[drag-drop]") {
    nk::Window window({.title = "External drop", .width = 180, .height = 100});
    auto target = DropProbeWidget::create();
    window.set_child(target);
    target->allocate({0.0F, 0.0F, 180.0F, 100.0F});

    int enters = 0;
    int drops = 0;
    auto enter_conn = target->on_drag_enter().connect([&](nk::DragDropEvent& event) {
        REQUIRE(event.external);
        ++enters;
    });
    auto drop_conn = target->on_drop().connect([&](nk::DragDropEvent& event) {
        REQUIRE(event.external);
        REQUIRE(event.payload != nullptr);
        REQUIRE(event.payload->has_files());
        REQUIRE(event.payload->files.size() == 2);
        REQUIRE(event.payload->files[0] == std::filesystem::path{"song.gridphonic"});
        event.accept(nk::DragOperation::Copy);
        ++drops;
    });
    (void)enter_conn;
    (void)drop_conn;

    auto payload = std::make_shared<nk::DragPayload>(nk::DragPayload::from_files(
        {std::filesystem::path{"song.gridphonic"}, std::filesystem::path{"render.wav"}}));
    const auto accepted = window.dispatch_drag_drop_event({
        .type = nk::DragDropEventType::Drop,
        .position = {60.0F, 40.0F},
        .payload = payload,
        .requested_operation = nk::DragOperation::Copy,
        .external = true,
    });

    REQUIRE(accepted == nk::DragOperation::Copy);
    REQUIRE(enters == 1);
    REQUIRE(drops == 1);
}
