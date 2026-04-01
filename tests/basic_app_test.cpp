/// @file basic_app_test.cpp
/// @brief Smoke test: create Application, Window, widgets, and quit.

#include <algorithm>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <nk/controllers/event_controller.h>
#include <nk/debug/diagnostics.h>
#include <nk/layout/box_layout.h>
#include <nk/layout/grid_layout.h>
#include <nk/layout/stack_layout.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/button.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/label.h>
#include <nk/widgets/list_view.h>
#include <nk/widgets/menu_bar.h>
#include <nk/widgets/scroll_area.h>
#include <nk/widgets/text_field.h>
#include <string>

namespace {

class FixedWidget : public nk::Widget {
public:
    static std::shared_ptr<FixedWidget> create(float width, float height) {
        return std::shared_ptr<FixedWidget>(new FixedWidget(width, height));
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, height_, width_, height_};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override { nk::Widget::snapshot(ctx); }

private:
    FixedWidget(float width, float height) : width_(width), height_(height) {}

    float width_ = 0.0F;
    float height_ = 0.0F;
};

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

class ConstraintAwareWidget : public nk::Widget {
public:
    static std::shared_ptr<ConstraintAwareWidget> create() {
        return std::shared_ptr<ConstraintAwareWidget>(new ConstraintAwareWidget());
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& constraints) const override {
        last_constraints_ = constraints;
        const float height = constraints.max_width <= 120.0F ? 48.0F : 24.0F;
        return {40.0F, 20.0F, 80.0F, height};
    }

    [[nodiscard]] nk::Constraints last_constraints() const { return last_constraints_; }

protected:
    void snapshot(nk::SnapshotContext& /*ctx*/) const override {}

private:
    ConstraintAwareWidget() = default;

    mutable nk::Constraints last_constraints_{};
};

class FocusProbeWidget : public nk::Widget {
public:
    static std::shared_ptr<FocusProbeWidget> create(float width, float height) {
        return std::shared_ptr<FocusProbeWidget>(new FocusProbeWidget(width, height));
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, height_, width_, height_};
    }

protected:
    void snapshot(nk::SnapshotContext& /*ctx*/) const override {}

private:
    FocusProbeWidget(float width, float height) : width_(width), height_(height) {
        set_focusable(true);
    }

    float width_ = 0.0F;
    float height_ = 0.0F;
};

class PrimitiveClipWidget : public nk::Widget {
public:
    static std::shared_ptr<PrimitiveClipWidget> create(float width, float height) {
        return std::shared_ptr<PrimitiveClipWidget>(new PrimitiveClipWidget(width, height));
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, height_, width_, height_};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto rect = allocation();
        ctx.add_rounded_rect(rect, nk::Color::from_rgb(235, 241, 247), 12.0F);
        ctx.push_rounded_clip(
            {rect.x + 8.0F, rect.y + 8.0F, rect.width - 16.0F, rect.height - 16.0F}, 10.0F);
        ctx.add_color_rect({rect.x - 12.0F, rect.y + 18.0F, rect.width * 0.7F, rect.height * 0.45F},
                           nk::Color::from_rgb(26, 153, 150));
        ctx.add_border({rect.x + 14.0F, rect.y + 14.0F, rect.width - 28.0F, rect.height - 28.0F},
                       nk::Color::from_rgb(12, 87, 84),
                       3.0F,
                       14.0F);
        ctx.pop_container();
    }

private:
    PrimitiveClipWidget(float width, float height) : width_(width), height_(height) {}

    float width_ = 0.0F;
    float height_ = 0.0F;
};

class ImageClipWidget : public nk::Widget {
public:
    static std::shared_ptr<ImageClipWidget> create(float width, float height) {
        return std::shared_ptr<ImageClipWidget>(new ImageClipWidget(width, height));
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, height_, width_, height_};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        static constexpr std::array<uint32_t, 16> kPixels = {
            0xFF0C6B68,
            0xFF149E98,
            0xFF2AB7AF,
            0xFF77D3CC,
            0xFF1E3D7B,
            0xFF3359AA,
            0xFF567BD1,
            0xFF8BAAF0,
            0xFF6A167C,
            0xFF8F229F,
            0xFFB34DC1,
            0xFFD08ADF,
            0xFF2A3947,
            0xFF526579,
            0xFF7E93A9,
            0xFFB8C5D3,
        };

        const auto rect = allocation();
        ctx.add_rounded_rect(rect, nk::Color::from_rgb(239, 243, 247), 14.0F);
        ctx.push_rounded_clip(
            {rect.x + 10.0F, rect.y + 10.0F, rect.width - 20.0F, rect.height - 20.0F}, 12.0F);
        ctx.add_image({rect.x + 4.0F, rect.y - 12.0F, rect.width * 0.78F, rect.height * 0.92F},
                      kPixels.data(),
                      4,
                      4,
                      nk::ScaleMode::Bilinear);
        ctx.pop_container();
    }

private:
    ImageClipWidget(float width, float height) : width_(width), height_(height) {}

    float width_ = 0.0F;
    float height_ = 0.0F;
};

class MixedGpuWidget : public nk::Widget {
public:
    static std::shared_ptr<MixedGpuWidget> create(float width, float height) {
        return std::shared_ptr<MixedGpuWidget>(new MixedGpuWidget(width, height));
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, height_, width_, height_};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        static constexpr std::array<uint32_t, 9> kPixels = {
            0xFF0C6B68,
            0xFF149E98,
            0xFF2AB7AF,
            0xFF3359AA,
            0xFF567BD1,
            0xFF8BAAF0,
            0xFF8F229F,
            0xFFB34DC1,
            0xFFD08ADF,
        };

        const auto rect = allocation();
        ctx.add_rounded_rect(rect, nk::Color::from_rgb(243, 246, 249), 12.0F);
        ctx.add_text({rect.x + 16.0F, rect.y + 18.0F},
                     "Metal text + image",
                     nk::Color::from_rgb(30, 44, 63));
        ctx.push_rounded_clip(
            {rect.x + 14.0F, rect.y + 42.0F, rect.width - 28.0F, rect.height - 56.0F}, 10.0F);
        ctx.add_image({rect.x + 10.0F, rect.y + 34.0F, rect.width * 0.65F, rect.height * 0.58F},
                      kPixels.data(),
                      3,
                      3,
                      nk::ScaleMode::Bilinear);
        ctx.pop_container();
    }

private:
    MixedGpuWidget(float width, float height) : width_(width), height_(height) {}

    float width_ = 0.0F;
    float height_ = 0.0F;
};

} // namespace

TEST_CASE("Application lifecycle can enter and exit the event loop", "[app]") {
    nk::Application app(0, nullptr);
    app.event_loop().post([&app] { app.quit(0); });
    int code = app.run();
    REQUIRE(code == 0);
}

TEST_CASE("Window and basic widgets support smoke-test interactions", "[app]") {
    nk::Window window({.title = "Test", .width = 320, .height = 240});

    auto label = nk::Label::create("Hello");
    REQUIRE(label->text() == "Hello");

    auto button = nk::Button::create("Click");
    int clicked = 0;
    auto conn = button->on_clicked().connect([&] { ++clicked; });
    (void)conn;

    button->on_clicked().emit();
    REQUIRE(clicked == 1);

    auto field = nk::TextField::create("initial");
    REQUIRE(field->text() == "initial");
    field->set_text("changed");
    REQUIRE(field->text() == "changed");

    window.set_child(label);
    window.present();
    REQUIRE(window.is_visible());
    window.hide();
    REQUIRE_FALSE(window.is_visible());
}

TEST_CASE("Window exposes the active renderer backend", "[app][render]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Renderer backend", .width = 320, .height = 240});
    window.set_child(nk::Label::create("Renderer"));

    window.present();
    REQUIRE(app.event_loop().poll());

    const auto expected_backend = (window.native_surface() != nullptr &&
                                   nk::renderer_backend_available(nk::RendererBackend::Metal))
                                      ? nk::RendererBackend::Metal
                                      : nk::RendererBackend::Software;
    REQUIRE(window.renderer_backend() == expected_backend);
    REQUIRE_FALSE(nk::renderer_backend_name(window.renderer_backend()).empty());
    REQUIRE(nk::renderer_backend_is_gpu(window.renderer_backend()) ==
            (window.renderer_backend() != nk::RendererBackend::Software));
}

TEST_CASE("Window presents clipped primitive content on the active renderer backend",
          "[app][render]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Primitive renderer", .width = 240, .height = 180});
    auto widget = PrimitiveClipWidget::create(180.0F, 120.0F);
    window.set_child(widget);

    window.present();
    REQUIRE(app.event_loop().poll());

    REQUIRE_FALSE(nk::renderer_backend_name(window.renderer_backend()).empty());
}

TEST_CASE("Window presents clipped image content on the active renderer backend", "[app][render]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Image renderer", .width = 260, .height = 200});
    auto widget = ImageClipWidget::create(200.0F, 140.0F);
    window.set_child(widget);

    window.present();
    REQUIRE(app.event_loop().poll());

    REQUIRE_FALSE(nk::renderer_backend_name(window.renderer_backend()).empty());
}

TEST_CASE("Window repeatedly presents mixed text and image content on the active renderer backend",
          "[app][render]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Mixed renderer", .width = 300, .height = 220});
    auto widget = MixedGpuWidget::create(220.0F, 150.0F);
    window.set_child(widget);

    window.present();
    REQUIRE(app.event_loop().poll());

    window.request_frame();
    REQUIRE(app.event_loop().poll());

    REQUIRE_FALSE(nk::renderer_backend_name(window.renderer_backend()).empty());
}

TEST_CASE("Window close requests are notification-only and hide the window", "[app]") {
    nk::Window window({.title = "Close test", .width = 320, .height = 240});

    bool close_requested = false;
    auto conn = window.on_close_request().connect([&close_requested] { close_requested = true; });
    (void)conn;

    window.present();
    REQUIRE(window.is_visible());

    window.close();
    REQUIRE(close_requested);
    REQUIRE_FALSE(window.is_visible());
}

TEST_CASE("Application reports file-dialog capability explicitly", "[app]") {
    nk::Application app(0, nullptr);

    if (app.supports_open_file_dialog()) {
        SUCCEED("Open-file dialogs are supported on this platform");
        return;
    }

    auto result = app.open_file_dialog();
    REQUIRE_FALSE(result);
    REQUIRE(result.error() == (app.has_platform_backend() ? nk::FileDialogError::Unsupported
                                                          : nk::FileDialogError::Unavailable));
}

TEST_CASE("ScrollArea clamps offsets and consumes bubbled scroll input", "[app][scroll]") {
    nk::Window window({.title = "Scroll test", .width = 120, .height = 80});
    auto scroll_area = nk::ScrollArea::create();
    auto content = FixedWidget::create(200.0F, 300.0F);
    scroll_area->set_content(content);
    window.set_child(scroll_area);

    scroll_area->allocate({0.0F, 0.0F, 120.0F, 80.0F});

    float reported_h = 0.0F;
    float reported_v = 0.0F;
    auto conn = scroll_area->on_scroll_changed().connect([&](float h, float v) {
        reported_h = h;
        reported_v = v;
    });
    (void)conn;

    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Scroll,
        .x = 40.0F,
        .y = 40.0F,
        .scroll_dx = 0.0F,
        .scroll_dy = -1.0F,
    });
    REQUIRE(scroll_area->v_offset() == Catch::Approx(40.0F));

    scroll_area->scroll_to(1000.0F, 1000.0F);
    REQUIRE(scroll_area->h_offset() == Catch::Approx(80.0F));
    REQUIRE(scroll_area->v_offset() == Catch::Approx(220.0F));
    REQUIRE(reported_h == Catch::Approx(80.0F));
    REQUIRE(reported_v == Catch::Approx(220.0F));
}

TEST_CASE("MenuBar opens popups and emits actions on click", "[app][menu]") {
    auto menu_bar = nk::MenuBar::create();
    menu_bar->add_menu({
        .title = "File",
        .items =
            {
                nk::MenuItem::action("Open", "app.open"),
                nk::MenuItem::submenu("Export", {nk::MenuItem::action("PNG", "app.export.png")}),
            },
    });
    menu_bar->allocate({0.0F, 0.0F, 320.0F, 32.0F});

    std::string action;
    auto conn =
        menu_bar->on_action().connect([&](std::string_view value) { action = std::string(value); });
    (void)conn;

    REQUIRE(menu_bar->handle_mouse_event({
        .type = nk::MouseEvent::Type::Press,
        .x = 20.0F,
        .y = 16.0F,
        .button = 1,
    }));
    REQUIRE(menu_bar->handle_mouse_event({
        .type = nk::MouseEvent::Type::Release,
        .x = 20.0F,
        .y = 16.0F,
        .button = 1,
    }));

    REQUIRE(menu_bar->handle_mouse_event({
        .type = nk::MouseEvent::Type::Press,
        .x = 20.0F,
        .y = 48.0F,
        .button = 1,
    }));
    REQUIRE(menu_bar->handle_mouse_event({
        .type = nk::MouseEvent::Type::Release,
        .x = 20.0F,
        .y = 48.0F,
        .button = 1,
    }));
    REQUIRE(action == "app.open");
}

TEST_CASE("Dialog present installs a modal overlay and routes Escape dismissal", "[app][dialog]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Dialog test", .width = 320, .height = 240});
    auto dialog = nk::Dialog::create("Confirm", "Proceed?");
    dialog->add_button("Cancel", nk::DialogResponse::Cancel);

    nk::DialogResponse response = nk::DialogResponse::None;
    auto conn = dialog->on_response().connect([&](nk::DialogResponse value) { response = value; });
    (void)conn;

    dialog->present(window);
    REQUIRE(dialog->is_presented());

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Escape,
    });

    REQUIRE(response == nk::DialogResponse::Cancel);
    REQUIRE_FALSE(dialog->is_presented());
}

TEST_CASE("Window dispatches pointer, keyboard, and focus controllers", "[app][controllers]") {
    nk::Window window({.title = "Controller test", .width = 240, .height = 120});
    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
    layout->set_spacing(8.0F);
    root->set_layout_manager(std::move(layout));

    auto first = FocusProbeWidget::create(80.0F, 32.0F);
    auto second = FocusProbeWidget::create(80.0F, 32.0F);
    root->append(first);
    root->append(second);
    window.set_child(root);
    root->allocate({0.0F, 0.0F, 200.0F, 40.0F});

    auto pointer = std::make_shared<nk::PointerController>();
    int enter_count = 0;
    int motion_count = 0;
    int press_count = 0;
    int release_count = 0;
    auto enter_conn = pointer->on_enter().connect([&](float, float) { ++enter_count; });
    auto motion_conn = pointer->on_motion().connect([&](float, float) { ++motion_count; });
    auto press_conn = pointer->on_pressed().connect([&](float, float, int) { ++press_count; });
    auto release_conn = pointer->on_released().connect([&](float, float, int) { ++release_count; });
    (void)enter_conn;
    (void)motion_conn;
    (void)press_conn;
    (void)release_conn;
    first->add_controller(pointer);

    auto keyboard = std::make_shared<nk::KeyboardController>();
    int key_press_count = 0;
    auto key_conn = keyboard->on_key_pressed().connect([&](int, int) { ++key_press_count; });
    (void)key_conn;
    first->add_controller(keyboard);

    auto focus_first = std::make_shared<nk::FocusController>();
    int first_focus_in = 0;
    int first_focus_out = 0;
    auto first_in_conn = focus_first->on_focus_in().connect([&] { ++first_focus_in; });
    auto first_out_conn = focus_first->on_focus_out().connect([&] { ++first_focus_out; });
    (void)first_in_conn;
    (void)first_out_conn;
    first->add_controller(focus_first);

    auto focus_second = std::make_shared<nk::FocusController>();
    int second_focus_in = 0;
    auto second_in_conn = focus_second->on_focus_in().connect([&] { ++second_focus_in; });
    (void)second_in_conn;
    second->add_controller(focus_second);

    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Move,
        .x = 20.0F,
        .y = 16.0F,
    });
    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Press,
        .x = 20.0F,
        .y = 16.0F,
        .button = 1,
    });
    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Release,
        .x = 20.0F,
        .y = 16.0F,
        .button = 1,
    });
    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::A,
    });
    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Tab,
    });

    REQUIRE(enter_count >= 1);
    REQUIRE(motion_count >= 1);
    REQUIRE(press_count == 1);
    REQUIRE(release_count == 1);
    REQUIRE(key_press_count == 1);
    REQUIRE(first_focus_in == 1);
    REQUIRE(first_focus_out == 1);
    REQUIRE(second_focus_in == 1);
}

TEST_CASE("Window restores focused widgets across focus changes and modal overlays",
          "[app][focus]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Focus restore", .width = 240, .height = 160});

    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
    layout->set_spacing(8.0F);
    root->set_layout_manager(std::move(layout));

    auto field = nk::TextField::create("");
    auto button = nk::Button::create("Action");
    root->append(field);
    root->append(button);
    window.set_child(root);
    root->allocate({0.0F, 0.0F, 220.0F, 100.0F});

    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Press,
        .x = 20.0F,
        .y = 20.0F,
        .button = 1,
    });
    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Release,
        .x = 20.0F,
        .y = 20.0F,
        .button = 1,
    });

    window.dispatch_window_event({.type = nk::WindowEvent::Type::FocusOut});
    window.dispatch_window_event({.type = nk::WindowEvent::Type::FocusIn});
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::A});
    REQUIRE(field->text() == "a");

    auto dialog = nk::Dialog::create("Confirm", "Keep editing?");
    dialog->add_button("Cancel", nk::DialogResponse::Cancel);
    dialog->present(window);
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Escape});
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::B});
    REQUIRE(field->text() == "ab");
}

TEST_CASE("Window tracks current key state and cursor shape", "[app][input]") {
    nk::Window window({.title = "Input state", .width = 240, .height = 120});
    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
    layout->set_spacing(8.0F);
    root->set_layout_manager(std::move(layout));

    auto button = nk::Button::create("Hover");
    auto field = nk::TextField::create("");
    root->append(button);
    root->append(field);
    window.set_child(root);
    root->allocate({0.0F, 0.0F, 220.0F, 100.0F});

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::LeftShift});
    REQUIRE(window.is_key_pressed(nk::KeyCode::LeftShift));

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Release, .key = nk::KeyCode::LeftShift});
    REQUIRE_FALSE(window.is_key_pressed(nk::KeyCode::LeftShift));

    window.dispatch_mouse_event({.type = nk::MouseEvent::Type::Move, .x = 20.0F, .y = 20.0F});
    REQUIRE(window.current_cursor_shape() == nk::CursorShape::PointingHand);

    window.dispatch_mouse_event({.type = nk::MouseEvent::Type::Move, .x = 20.0F, .y = 64.0F});
    REQUIRE(window.current_cursor_shape() == nk::CursorShape::IBeam);

    window.dispatch_mouse_event({.type = nk::MouseEvent::Type::Leave, .x = 0.0F, .y = 0.0F});
    REQUIRE(window.current_cursor_shape() == nk::CursorShape::Default);
}

TEST_CASE("Window captures frame diagnostics and widget debug dumps", "[app][debug]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Debug", .width = 240, .height = 120});

    auto root = TestContainer::create();
    root->set_debug_name("root");
    root->add_style_class("root-surface");
    auto child = FixedWidget::create(120.0F, 32.0F);
    child->set_debug_name("leaf");
    root->append(child);

    window.set_child(root);
    window.set_debug_overlay_flags(nk::DebugOverlayFlags::LayoutBounds |
                                   nk::DebugOverlayFlags::DirtyWidgets |
                                   nk::DebugOverlayFlags::FrameHud);
    window.present();

    REQUIRE(app.event_loop().poll());

    const auto& diagnostics = window.last_frame_diagnostics();
    REQUIRE(diagnostics.frame_id >= 1);
    REQUIRE(diagnostics.widget_count >= 2);
    REQUIRE(diagnostics.render_node_count >= 3);
    REQUIRE(diagnostics.total_ms >= 0.0);

    const auto tree = window.debug_tree();
    REQUIRE(tree.type_name == "Window");
    REQUIRE(tree.children.size() == 1);
    REQUIRE(tree.children.front().debug_name == "root");
    REQUIRE(tree.children.front().children.size() == 1);
    REQUIRE(tree.children.front().children.front().debug_name == "leaf");

    const auto dump = window.dump_widget_tree();
    REQUIRE(dump.find("Window \"Debug\"") != std::string::npos);
    REQUIRE(dump.find("root-surface") != std::string::npos);
    REQUIRE(dump.find("\"leaf\"") != std::string::npos);
}

TEST_CASE("Window retains frame history and exports trace JSON", "[app][debug]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Trace", .width = 240, .height = 120});
    app.event_loop().clear_debug_trace_events();

    auto root = TestContainer::create();
    root->set_debug_name("trace-root");
    auto child = nk::Label::create("Trace");
    child->set_debug_name("trace-label");
    root->append(child);

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    child->queue_redraw();
    REQUIRE(app.event_loop().poll());

    child->queue_layout();
    REQUIRE(app.event_loop().poll());

    const auto history = window.debug_frame_history();
    REQUIRE(history.size() >= 3);
    REQUIRE(has_frame_request_reason(history.front(), nk::FrameRequestReason::Present));
    REQUIRE(has_frame_request_reason(history[history.size() - 2],
                                     nk::FrameRequestReason::WidgetRedraw));
    REQUIRE(has_frame_request_reason(history.back(), nk::FrameRequestReason::WidgetLayout));
    REQUIRE(history.back().started_at_ms >= history.front().requested_at_ms);
    REQUIRE(history.back().render_snapshot_node_count >= 3);

    const auto selected_snapshot = window.debug_selected_frame_render_snapshot();
    REQUIRE(selected_snapshot.kind == "Container");
    REQUIRE_FALSE(selected_snapshot.children.empty());

    const auto snapshot_dump = window.dump_selected_frame_render_snapshot();
    REQUIRE(snapshot_dump.find("Container") != std::string::npos);

    const auto snapshot_json = window.dump_selected_frame_render_snapshot_json();
    REQUIRE(snapshot_json.find("\"kind\":\"Container\"") != std::string::npos);
    REQUIRE(snapshot_json.find("trace-label") != std::string::npos);

    window.set_debug_selected_widget(child.get());
    const auto widget_selected_render = window.debug_selected_render_node();
    REQUIRE(widget_selected_render.kind == "Text");
    REQUIRE(widget_selected_render.source_widget_label == "trace-label");
    REQUIRE(widget_selected_render.detail.find("Trace") != std::string::npos);

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::I,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(nk::has_debug_overlay_flag(window.debug_overlay_flags(),
                                       nk::DebugOverlayFlags::InspectorPanel));

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::PageDown,
    });
    const auto selected_render = window.debug_selected_render_node();
    REQUIRE_FALSE(selected_render.kind.empty());
    REQUIRE(selected_render.children.size() < selected_snapshot.children.size());

    window.set_debug_selected_widget(nullptr);
    bool found_provenance_selected_render = false;
    for (int step = 0; step < 8; ++step) {
        window.dispatch_key_event({
            .type = nk::KeyEvent::Type::Press,
            .key = nk::KeyCode::PageDown,
        });
        const auto render_node = window.debug_selected_render_node();
        if (render_node.source_widget_label == "trace-label") {
            found_provenance_selected_render = true;
            break;
        }
    }
    REQUIRE(found_provenance_selected_render);
    REQUIRE(window.debug_selected_widget() == child.get());

    bool posted_ran = false;
    bool timeout_ran = false;
    bool idle_ran = false;
    app.event_loop().post([&] { posted_ran = true; });
    (void)app.event_loop().set_timeout(std::chrono::milliseconds{0}, [&] { timeout_ran = true; });
    (void)app.event_loop().add_idle([&] { idle_ran = true; });
    REQUIRE(app.event_loop().poll());
    REQUIRE(posted_ran);
    REQUIRE(timeout_ran);
    REQUIRE(app.event_loop().poll());
    REQUIRE(idle_ran);

    const auto runtime_trace = app.event_loop().debug_trace_events();
    REQUIRE_FALSE(runtime_trace.empty());
    REQUIRE(std::any_of(runtime_trace.begin(),
                        runtime_trace.end(),
                        [](const nk::TraceEvent& event) { return event.name == "posted-task"; }));
    REQUIRE(std::any_of(runtime_trace.begin(),
                        runtime_trace.end(),
                        [](const nk::TraceEvent& event) { return event.name == "timeout"; }));
    REQUIRE(std::any_of(runtime_trace.begin(),
                        runtime_trace.end(),
                        [](const nk::TraceEvent& event) { return event.name == "idle"; }));

    const auto trace = window.dump_frame_trace_json();
    REQUIRE(trace.find("\"traceEvents\"") != std::string::npos);
    REQUIRE(trace.find("\"name\":\"widget-redraw\"") != std::string::npos);
    REQUIRE(trace.find("\"name\":\"layout\"") != std::string::npos);
    REQUIRE(trace.find("\"name\":\"frame\"") != std::string::npos);
    REQUIRE(trace.find("\"name\":\"posted-task\"") != std::string::npos);
}

TEST_CASE("Window debug picker selects widgets and honors inspector shortcuts", "[app][debug]") {
    nk::Window window({.title = "Inspector", .width = 240, .height = 120});
    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
    root->set_layout_manager(std::move(layout));

    auto first = FocusProbeWidget::create(80.0F, 40.0F);
    first->set_debug_name("first");
    auto second = FocusProbeWidget::create(80.0F, 40.0F);
    second->set_debug_name("second");
    root->append(first);
    root->append(second);
    window.set_child(root);
    root->allocate({0.0F, 0.0F, 200.0F, 60.0F});

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::I,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(nk::has_debug_overlay_flag(window.debug_overlay_flags(),
                                       nk::DebugOverlayFlags::InspectorPanel));

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::P,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(window.debug_picker_enabled());

    window.dispatch_mouse_event({.type = nk::MouseEvent::Type::Move, .x = 20.0F, .y = 20.0F});
    REQUIRE(window.debug_selected_widget() == first.get());
    REQUIRE(window.current_cursor_shape() == nk::CursorShape::Default);

    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Press,
        .x = 20.0F,
        .y = 20.0F,
        .button = 1,
    });
    REQUIRE_FALSE(window.debug_picker_enabled());
    REQUIRE(window.debug_selected_widget() == first.get());

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Down,
    });
    REQUIRE(window.debug_selected_widget() == second.get());

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Home,
    });
    REQUIRE(window.debug_selected_widget() == first.get());

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::P,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(window.debug_picker_enabled());
    window.dispatch_mouse_event({.type = nk::MouseEvent::Type::Move, .x = 120.0F, .y = 20.0F});
    REQUIRE(window.debug_selected_widget() == second.get());

    const auto selected = window.debug_selected_widget_info();
    REQUIRE(selected.debug_name == "second");

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Escape,
    });
    REQUIRE_FALSE(window.debug_picker_enabled());

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::I,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE_FALSE(nk::has_debug_overlay_flag(window.debug_overlay_flags(),
                                             nk::DebugOverlayFlags::InspectorPanel));
}

TEST_CASE("TextField supports caret movement, selection, clipboard, and undo", "[app][text]") {
    nk::Application app(0, nullptr);
    auto field = nk::TextField::create("abcd");
    field->allocate({0.0F, 0.0F, 220.0F, 36.0F});

    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left});
    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left});
    REQUIRE(field->cursor_position() == 2);

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Right,
        .modifiers = nk::Modifiers::Shift,
    });
    REQUIRE(field->has_selection());
    REQUIRE(field->selection_start() == 2);
    REQUIRE(field->selection_end() == 3);

    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Backspace});
    REQUIRE(field->text() == "abd");
    REQUIRE(field->cursor_position() == 2);

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::X,
        .modifiers = nk::Modifiers::Shift,
    });
    REQUIRE(field->text() == "abXd");
    REQUIRE(field->cursor_position() == 3);

    field->select_all();
    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::C,
        .modifiers = nk::Modifiers::Ctrl,
    });
    REQUIRE(app.clipboard_text() == "abXd");

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::X,
        .modifiers = nk::Modifiers::Ctrl,
    });
    REQUIRE(field->text().empty());

    app.set_clipboard_text("restored");
    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::V,
        .modifiers = nk::Modifiers::Ctrl,
    });
    REQUIRE(field->text() == "restored");

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Z,
        .modifiers = nk::Modifiers::Ctrl,
    });
    REQUIRE(field->text().empty());

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Z,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(field->text() == "restored");
}

TEST_CASE("StringListModel supports append and remove", "[app]") {
    nk::StringListModel model({"Alpha", "Beta", "Gamma"});
    REQUIRE(model.row_count() == 3);
    REQUIRE(model.display_text(0) == "Alpha");
    REQUIRE(model.display_text(2) == "Gamma");

    model.append("Delta");
    REQUIRE(model.row_count() == 4);

    model.remove(1);
    REQUIRE(model.row_count() == 3);
    REQUIRE(model.display_text(1) == "Gamma");
}

TEST_CASE("StringListModel row signals preserve inserted and removed ranges", "[app][model]") {
    nk::StringListModel model({"Alpha"});

    std::size_t inserted_first = 0;
    std::size_t inserted_count = 0;
    std::size_t removed_first = 0;
    std::size_t removed_count = 0;
    auto insert_conn = model.on_rows_inserted().connect([&](std::size_t first, std::size_t count) {
        inserted_first = first;
        inserted_count = count;
    });
    auto remove_conn = model.on_rows_removed().connect([&](std::size_t first, std::size_t count) {
        removed_first = first;
        removed_count = count;
    });
    (void)insert_conn;
    (void)remove_conn;

    model.append("Beta");
    REQUIRE(inserted_first == 1);
    REQUIRE(inserted_count == 1);

    model.remove(0);
    REQUIRE(removed_first == 0);
    REQUIRE(removed_count == 1);
}

TEST_CASE("BoxLayout passes constraints and distributes extra space by stretch", "[app][layout]") {
    auto container = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
    container->set_layout_manager(std::move(layout));

    auto constrained = ConstraintAwareWidget::create();
    auto expand_a = FixedWidget::create(40.0F, 20.0F);
    auto expand_b = FixedWidget::create(40.0F, 20.0F);
    expand_a->set_vertical_size_policy(nk::SizePolicy::Expanding);
    expand_a->set_vertical_stretch(2);
    expand_b->set_vertical_size_policy(nk::SizePolicy::Expanding);
    expand_b->set_vertical_stretch(1);
    container->append(constrained);
    container->append(expand_a);
    container->append(expand_b);

    const auto request = container->measure({0.0F, 0.0F, 100.0F, 300.0F});
    REQUIRE(constrained->last_constraints().max_width == Catch::Approx(100.0F));
    REQUIRE(request.natural_height == Catch::Approx(88.0F));

    container->allocate({0.0F, 0.0F, 100.0F, 160.0F});
    REQUIRE(expand_a->allocation().height == Catch::Approx(68.0F));
    REQUIRE(expand_b->allocation().height == Catch::Approx(44.0F));
}

TEST_CASE("StackLayout overlays visible children in the same allocation", "[app][layout]") {
    auto container = TestContainer::create();
    container->set_layout_manager(std::make_unique<nk::StackLayout>());

    auto first = FixedWidget::create(80.0F, 40.0F);
    auto second = FixedWidget::create(60.0F, 30.0F);
    container->append(first);
    container->append(second);

    const auto request = container->measure(nk::Constraints::unbounded());
    REQUIRE(request.natural_width == Catch::Approx(80.0F));
    REQUIRE(request.natural_height == Catch::Approx(40.0F));

    container->allocate({4.0F, 6.0F, 120.0F, 90.0F});
    REQUIRE(first->allocation() == nk::Rect{4.0F, 6.0F, 120.0F, 90.0F});
    REQUIRE(second->allocation() == nk::Rect{4.0F, 6.0F, 120.0F, 90.0F});
}

TEST_CASE("GridLayout places children by row, column, and span", "[app][layout]") {
    auto container = TestContainer::create();
    auto layout = std::make_unique<nk::GridLayout>();
    auto* grid = layout.get();
    grid->set_row_spacing(6.0F);
    grid->set_column_spacing(4.0F);
    container->set_layout_manager(std::move(layout));

    auto top_left = FixedWidget::create(40.0F, 20.0F);
    auto top_right = FixedWidget::create(60.0F, 30.0F);
    auto spanning = FixedWidget::create(120.0F, 40.0F);

    container->append(top_left);
    container->append(top_right);
    container->append(spanning);
    grid->attach(*top_left, 0, 0);
    grid->attach(*top_right, 0, 1);
    grid->attach(*spanning, 1, 0, 1, 2);

    const auto request = container->measure(nk::Constraints::unbounded());
    REQUIRE(request.natural_width == Catch::Approx(124.0F));
    REQUIRE(request.natural_height == Catch::Approx(76.0F));

    container->allocate({0.0F, 0.0F, 124.0F, 76.0F});
    REQUIRE(top_left->allocation() == nk::Rect{0.0F, 0.0F, 60.0F, 30.0F});
    REQUIRE(top_right->allocation() == nk::Rect{64.0F, 0.0F, 60.0F, 30.0F});
    REQUIRE(spanning->allocation() == nk::Rect{0.0F, 36.0F, 124.0F, 40.0F});
}

TEST_CASE("ListView item factories materialize visible rows and refresh on scroll", "[app][list]") {
    auto model = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{"Alpha", "Beta", "Gamma", "Delta", "Epsilon"});
    auto list_view = nk::ListView::create();
    list_view->set_model(model);

    int factory_calls = 0;
    list_view->set_item_factory([&](std::size_t row) {
        ++factory_calls;
        return nk::Label::create(model->display_text(row));
    });

    list_view->allocate({0.0F, 0.0F, 180.0F, 72.0F});
    REQUIRE(factory_calls >= 3);
    REQUIRE_FALSE(list_view->children().empty());

    list_view->handle_mouse_event({
        .type = nk::MouseEvent::Type::Scroll,
        .x = 20.0F,
        .y = 20.0F,
        .scroll_dx = 0.0F,
        .scroll_dy = -1.0F,
    });

    bool saw_beta = false;
    for (const auto& child : list_view->children()) {
        auto* label = dynamic_cast<nk::Label*>(child.get());
        if (label != nullptr && label->text() == "Beta") {
            saw_beta = true;
        }
    }
    REQUIRE(saw_beta);
}

TEST_CASE("SelectionModel single mode deselects previous rows", "[app]") {
    nk::SelectionModel sel(nk::SelectionMode::Single);
    sel.select(2);
    REQUIRE(sel.is_selected(2));
    REQUIRE_FALSE(sel.is_selected(0));

    sel.select(5);
    REQUIRE(sel.is_selected(5));
    REQUIRE_FALSE(sel.is_selected(2));
}
