/// @file basic_app_test.cpp
/// @brief Smoke test: create Application, Window, widgets, and quit.

#include <algorithm>
#include <any>
#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nk/accessibility/atspi_bridge.h>
#include <nk/animation/easing.h>
#include <nk/animation/timed_animation.h>
#include <nk/controllers/event_controller.h>
#include <nk/debug/diagnostics.h>
#include <nk/layout/box_layout.h>
#include <nk/layout/grid_layout.h>
#include <nk/layout/stack_layout.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/list_model_adapters.h>
#include <nk/model/selection_model.h>
#include <nk/model/tree_model.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/platform_backend.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/style/style_class.h>
#include <nk/style/theme.h>
#include <nk/text/font.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/command_palette.h>
#include <nk/widgets/data_table.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/grid_view.h>
#include <nk/widgets/image_view.h>
#include <nk/widgets/label.h>
#include <nk/widgets/list_view.h>
#include <nk/widgets/menu_bar.h>
#include <nk/widgets/scroll_area.h>
#include <nk/widgets/segmented_control.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/text_field.h>
#include <nk/widgets/tree_view.h>
#include <optional>
#include <sstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {

#if defined(_WIN32)
int set_test_env(const char* name, const char* value) {
    return _putenv_s(name, value);
}

int unset_test_env(const char* name) {
    return _putenv_s(name, "");
}

RECT suggested_window_rect_for_dpi(HWND hwnd, nk::Size logical_size, UINT dpi) {
    RECT current_window_rect{};
    GetWindowRect(hwnd, &current_window_rect);
    RECT current_client_rect{};
    GetClientRect(hwnd, &current_client_rect);

    const int current_window_width = current_window_rect.right - current_window_rect.left;
    const int current_window_height = current_window_rect.bottom - current_window_rect.top;
    const int current_client_width = current_client_rect.right - current_client_rect.left;
    const int current_client_height = current_client_rect.bottom - current_client_rect.top;
    const int non_client_width = current_window_width - current_client_width;
    const int non_client_height = current_window_height - current_client_height;

    const int target_client_width = std::max(
        1, static_cast<int>(std::lround(logical_size.width * static_cast<float>(dpi) / 96.0F)));
    const int target_client_height = std::max(
        1, static_cast<int>(std::lround(logical_size.height * static_cast<float>(dpi) / 96.0F)));

    return {
        current_window_rect.left,
        current_window_rect.top,
        current_window_rect.left + non_client_width + target_client_width,
        current_window_rect.top + non_client_height + target_client_height,
    };
}
#else
int set_test_env(const char* name, const char* value) {
    return setenv(name, value, 1);
}

int unset_test_env(const char* name) {
    return unsetenv(name);
}
#endif

class ScopedTestEnvVar {
public:
    explicit ScopedTestEnvVar(const char* name) : name_(name) {
        if (const char* value = std::getenv(name_); value != nullptr) {
            previous_value_ = std::string(value);
        }
    }

    ~ScopedTestEnvVar() {
        if (previous_value_.has_value()) {
            (void)set_test_env(name_, previous_value_->c_str());
        } else {
            (void)unset_test_env(name_);
        }
    }

    int set(const char* value) { return set_test_env(name_, value); }

private:
    const char* name_;
    std::optional<std::string> previous_value_;
};

std::filesystem::path test_fixture_path(std::string_view relative) {
    const auto source_relative =
        std::filesystem::path(__FILE__).parent_path() / "fixtures" / relative;
    if (std::filesystem::exists(source_relative)) {
        return source_relative;
    }
    return std::filesystem::current_path() / "tests" / "fixtures" / relative;
}

const nk::RenderSnapshotNode* find_render_snapshot_node(const nk::RenderSnapshotNode& node,
                                                        std::string_view kind,
                                                        std::string_view detail_substring) {
    if (node.kind == kind &&
        (detail_substring.empty() || node.detail.find(detail_substring) != std::string::npos)) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const auto* match = find_render_snapshot_node(child, kind, detail_substring);
            match != nullptr) {
            return match;
        }
    }
    return nullptr;
}

float widest_render_snapshot_kind_containing(const nk::RenderSnapshotNode& node,
                                             nk::Point point,
                                             std::string_view kind) {
    float widest = (node.kind == kind && node.bounds.contains(point)) ? node.bounds.width : 0.0F;
    for (const auto& child : node.children) {
        widest = std::max(widest, widest_render_snapshot_kind_containing(child, point, kind));
    }
    return widest;
}

class FixedWidget : public nk::Widget {
public:
    static std::shared_ptr<FixedWidget> create(float width, float height) {
        return std::shared_ptr<FixedWidget>(new FixedWidget(width, height));
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, height_, width_, height_};
    }

    /// Expose theme_color for testing style cascade.
    [[nodiscard]] nk::Color test_theme_color(std::string_view name, nk::Color fallback) const {
        return theme_color(name, fallback);
    }

    [[nodiscard]] float test_theme_number(std::string_view name, float fallback) const {
        return theme_number(name, fallback);
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

class MeasuringMenuBar : public nk::MenuBar {
public:
    static std::shared_ptr<MeasuringMenuBar> create() {
        return std::shared_ptr<MeasuringMenuBar>(new MeasuringMenuBar());
    }

    [[nodiscard]] nk::Size menu_label_size(std::string_view text) const {
        return measure_text(text,
                            nk::FontDescriptor{
                                .family = {},
                                .size = 13.5F,
                                .weight = nk::FontWeight::Medium,
                            });
    }

private:
    MeasuringMenuBar() = default;
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

class ResizableColorWidget : public nk::Widget {
public:
    static std::shared_ptr<ResizableColorWidget>
    create(float width, float height, nk::Color color) {
        return std::shared_ptr<ResizableColorWidget>(
            new ResizableColorWidget(width, height, color));
    }

    void set_width(float width) {
        if (width_ == width) {
            return;
        }
        width_ = width;
        queue_layout();
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, height_, width_, height_};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        ctx.add_color_rect(allocation(), color_);
    }

private:
    ResizableColorWidget(float width, float height, nk::Color color)
        : width_(width), height_(height), color_(color) {}

    float width_ = 0.0F;
    float height_ = 0.0F;
    nk::Color color_{};
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

class LocalizedMixedGpuWidget : public nk::Widget {
public:
    static std::shared_ptr<LocalizedMixedGpuWidget> create(float width, float height) {
        return std::shared_ptr<LocalizedMixedGpuWidget>(new LocalizedMixedGpuWidget(width, height));
    }

    void set_title(std::string title) {
        if (title_ == title) {
            return;
        }
        title_ = std::move(title);
        queue_redraw(text_damage_rect());
    }

    void set_image_variant(std::size_t image_variant) {
        image_variant %= 2;
        if (image_variant_ == image_variant) {
            return;
        }
        image_variant_ = image_variant;
        queue_redraw(image_damage_rect());
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, height_, width_, height_};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        static constexpr std::array<uint32_t, 9> kPixelsA = {
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
        static constexpr std::array<uint32_t, 9> kPixelsB = {
            0xFF234A5E,
            0xFF2D6780,
            0xFF3F89A8,
            0xFF7A5C1B,
            0xFFB78427,
            0xFFE4B858,
            0xFF7E2539,
            0xFFB5405D,
            0xFFE27B97,
        };

        const auto rect = allocation();
        const auto* pixels = image_variant_ == 0 ? kPixelsA.data() : kPixelsB.data();
        ctx.add_rounded_rect(rect, nk::Color::from_rgb(243, 246, 249), 12.0F);
        ctx.add_text({rect.x + 16.0F, rect.y + 18.0F}, title_, nk::Color::from_rgb(30, 44, 63));
        ctx.push_rounded_clip(
            {rect.x + 14.0F, rect.y + 42.0F, rect.width - 28.0F, rect.height - 56.0F}, 10.0F);
        ctx.add_image({rect.x + 10.0F, rect.y + 34.0F, rect.width * 0.65F, rect.height * 0.58F},
                      pixels,
                      3,
                      3,
                      nk::ScaleMode::Bilinear);
        ctx.pop_container();
    }

private:
    LocalizedMixedGpuWidget(float width, float height) : width_(width), height_(height) {}

    [[nodiscard]] nk::Rect text_damage_rect() const {
        return {12.0F, 12.0F, width_ * 0.72F, 32.0F};
    }

    [[nodiscard]] nk::Rect image_damage_rect() const {
        return {18.0F, 38.0F, width_ * 0.68F, height_ * 0.62F};
    }

    float width_ = 0.0F;
    float height_ = 0.0F;
    std::string title_ = "Windows text + image";
    std::size_t image_variant_ = 0;
};

class MultiDamageImageWidget : public nk::Widget {
public:
    static std::shared_ptr<MultiDamageImageWidget> create(float width, float height) {
        return std::shared_ptr<MultiDamageImageWidget>(new MultiDamageImageWidget(width, height));
    }

    void queue_close_image_updates() {
        queue_redraw({24.0F, 30.0F, 14.0F, 14.0F});
        queue_redraw({42.0F, 30.0F, 14.0F, 14.0F});
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
        ctx.add_color_rect(rect, nk::Color::from_rgb(243, 246, 249));
        ctx.add_image({rect.x + 18.0F, rect.y + 22.0F, rect.width * 0.72F, rect.height * 0.62F},
                      kPixels.data(),
                      3,
                      3,
                      nk::ScaleMode::Bilinear);
    }

private:
    MultiDamageImageWidget(float width, float height) : width_(width), height_(height) {}

    float width_ = 0.0F;
    float height_ = 0.0F;
};

class UnsupportedSemanticGpuWidget : public nk::Widget {
public:
    static std::shared_ptr<UnsupportedSemanticGpuWidget> create(float width, float height) {
        return std::shared_ptr<UnsupportedSemanticGpuWidget>(
            new UnsupportedSemanticGpuWidget(width, height));
    }

    void set_accent(nk::Color color) {
        accent_ = color;
        queue_redraw({0.0F, 0.0F, width_, height_});
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, height_, width_, height_};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto rect = allocation();
        ctx.add_color_rect(rect, nk::Color::from_rgb(246, 248, 251));
        ctx.add_shadow({rect.x + 18.0F, rect.y + 18.0F, rect.width - 36.0F, rect.height - 36.0F},
                       nk::Color{0.0F, 0.0F, 0.0F, 0.22F},
                       0.0F,
                       3.0F,
                       8.0F,
                       0.0F,
                       14.0F);
        ctx.push_opacity({rect.x + 24.0F, rect.y + 74.0F, rect.width - 48.0F, 36.0F}, 0.65F);
        ctx.add_color_rect({rect.x + 24.0F, rect.y + 74.0F, rect.width - 48.0F, 36.0F}, accent_);
        ctx.pop_container();
    }

private:
    UnsupportedSemanticGpuWidget(float width, float height) : width_(width), height_(height) {}

    float width_ = 0.0F;
    float height_ = 0.0F;
    nk::Color accent_ = nk::Color::from_rgb(26, 153, 150);
};

class NativeGradientGpuWidget : public nk::Widget {
public:
    static std::shared_ptr<NativeGradientGpuWidget> create(float width, float height) {
        return std::shared_ptr<NativeGradientGpuWidget>(new NativeGradientGpuWidget(width, height));
    }

    void set_accent(nk::Color color) {
        accent_ = color;
        queue_redraw({16.0F, 18.0F, width_ - 32.0F, height_ - 36.0F});
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, height_, width_, height_};
    }

protected:
    void snapshot(nk::SnapshotContext& ctx) const override {
        const auto rect = allocation();
        ctx.add_color_rect(rect, nk::Color::from_rgb(245, 248, 251));
        ctx.push_rounded_clip(
            {rect.x + 16.0F, rect.y + 18.0F, rect.width - 32.0F, rect.height - 36.0F}, 12.0F);
        ctx.add_linear_gradient({rect.x + 16.0F, rect.y + 18.0F, rect.width - 32.0F, 44.0F},
                                nk::Color::from_rgb(232, 244, 243),
                                accent_,
                                nk::Orientation::Horizontal);
        ctx.add_linear_gradient(
            {rect.x + 16.0F, rect.y + 62.0F, rect.width - 32.0F, rect.height - 80.0F},
            accent_,
            nk::Color::from_rgb(248, 251, 253),
            nk::Orientation::Vertical);
        ctx.pop_container();
    }

private:
    NativeGradientGpuWidget(float width, float height) : width_(width), height_(height) {}

    float width_ = 0.0F;
    float height_ = 0.0F;
    nk::Color accent_ = nk::Color::from_rgb(26, 153, 150);
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

TEST_CASE("Dialog can close after caller releases its setup reference", "[app][dialog]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Dialog lifetime", .width = 320, .height = 240});
    window.set_child(nk::Label::create("Root"));
    window.present();
    REQUIRE(app.event_loop().poll());

    std::weak_ptr<nk::Dialog> weak_dialog;
    bool accepted = false;
    {
        auto dialog = nk::Dialog::create("About", "Dialog lifetime regression");
        dialog->add_button("OK", nk::DialogResponse::Accept);
        auto conn = dialog->on_response().connect([&](nk::DialogResponse response) {
            accepted = response == nk::DialogResponse::Accept;
        });
        (void)conn;
        weak_dialog = dialog;
        dialog->present(window);
    }

    REQUIRE_FALSE(weak_dialog.expired());
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return});
    REQUIRE(accepted);
    REQUIRE(weak_dialog.expired());
}

TEST_CASE("Window exposes the active renderer backend", "[app][render]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Renderer backend", .width = 320, .height = 240});
    window.set_child(nk::Label::create("Renderer"));

    window.present();
    REQUIRE(app.event_loop().poll());

    const auto support = window.native_surface() != nullptr
                             ? window.native_surface()->renderer_backend_support()
                             : nk::RendererBackendSupport{};
    const auto expected_backend =
#if defined(_WIN32)
        nk::renderer_backend_supported(support, nk::RendererBackend::D3D11) &&
                nk::renderer_backend_available(nk::RendererBackend::D3D11)
            ? nk::RendererBackend::D3D11
        :
#endif
        nk::renderer_backend_supported(support, nk::RendererBackend::Metal) &&
                nk::renderer_backend_available(nk::RendererBackend::Metal)
            ? nk::RendererBackend::Metal
        : nk::renderer_backend_supported(support, nk::RendererBackend::Vulkan) &&
                nk::renderer_backend_available(nk::RendererBackend::Vulkan)
            ? nk::RendererBackend::Vulkan
            : nk::RendererBackend::Software;
    REQUIRE(window.renderer_backend() == expected_backend);
    REQUIRE_FALSE(nk::renderer_backend_name(window.renderer_backend()).empty());
    REQUIRE(nk::renderer_backend_is_gpu(window.renderer_backend()) ==
            (window.renderer_backend() != nk::RendererBackend::Software));
}

TEST_CASE("Window honors a renderer backend override from the environment", "[app][render]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("software") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Renderer override", .width = 320, .height = 240});
    window.set_child(nk::Label::create("Renderer override"));

    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::Software);
}

#if defined(_WIN32)
TEST_CASE("Window honors a D3D11 renderer backend override from the environment",
          "[app][render][windows]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("d3d11") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "D3D11 renderer override", .width = 320, .height = 240});
    window.set_child(nk::Label::create("D3D11 renderer override"));

    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::D3D11);
}

TEST_CASE("Win32 D3D11 renders clipped image content on the GPU and reuses image textures",
          "[app][render][windows]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("d3d11") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "D3D11 image renderer", .width = 260, .height = 200});
    auto widget = ImageClipWidget::create(200.0F, 140.0F);
    window.set_child(widget);

    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::D3D11);

    const auto first_frame = window.last_frame_diagnostics();
    REQUIRE(first_frame.render_hotspot_counters.image_node_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.image_texture_upload_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.gpu_draw_call_count >= 2);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::FullRedrawCopyBack);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_tradeoff ==
            nk::GpuPresentTradeoff::DrawFavored);

    window.request_frame();
    REQUIRE(app.event_loop().poll());

    const auto second_frame = window.last_frame_diagnostics();
    REQUIRE(second_frame.render_hotspot_counters.image_node_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.image_texture_cache_hit_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.image_texture_upload_count == 0);
    REQUIRE(second_frame.render_hotspot_counters.gpu_draw_call_count >= 2);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::FullRedrawCopyBack);
}

TEST_CASE("Win32 D3D11 keeps mixed text and image content on the GPU", "[app][render][windows]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("d3d11") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "D3D11 mixed renderer", .width = 300, .height = 220});
    auto widget = MixedGpuWidget::create(220.0F, 150.0F);
    window.set_child(widget);

    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::D3D11);

    const auto first_frame = window.last_frame_diagnostics();
    REQUIRE(first_frame.render_hotspot_counters.text_node_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.image_node_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.text_shape_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.text_texture_upload_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.image_texture_upload_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.gpu_draw_call_count >= 3);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::FullRedrawCopyBack);

    window.request_frame();
    REQUIRE(app.event_loop().poll());

    const auto second_frame = window.last_frame_diagnostics();
    REQUIRE(second_frame.render_hotspot_counters.text_node_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.image_node_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.text_shape_cache_hit_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.text_texture_cache_hit_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.image_texture_cache_hit_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.text_texture_upload_count == 0);
    REQUIRE(second_frame.render_hotspot_counters.image_texture_upload_count == 0);
    REQUIRE(second_frame.render_hotspot_counters.gpu_draw_call_count >= 3);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::FullRedrawCopyBack);
}

TEST_CASE("Win32 D3D11 reduces mixed-content draw work for localized widget damage",
          "[app][render][windows]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("d3d11") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "D3D11 localized mixed renderer", .width = 300, .height = 220});
    auto widget = LocalizedMixedGpuWidget::create(220.0F, 150.0F);
    window.set_child(widget);

    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::D3D11);

    const auto first_frame = window.last_frame_diagnostics();
    REQUIRE(first_frame.render_hotspot_counters.text_node_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.image_node_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.gpu_draw_call_count >= 3);
    REQUIRE(first_frame.render_hotspot_counters.gpu_replayed_command_count >= 3);
    REQUIRE(first_frame.render_hotspot_counters.gpu_skipped_command_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::FullRedrawCopyBack);

    widget->set_title("Localized text update");
    REQUIRE(app.event_loop().poll());

    const auto second_frame = window.last_frame_diagnostics();
    REQUIRE_FALSE(second_frame.had_layout);
    REQUIRE(second_frame.render_hotspot_counters.damage_region_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_draw_call_count >= 2);
    REQUIRE(second_frame.render_hotspot_counters.gpu_replayed_command_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_replayed_command_count <=
            first_frame.render_hotspot_counters.gpu_replayed_command_count);
    REQUIRE(second_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count > 0);
    REQUIRE(second_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count <
            first_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count);
    REQUIRE(second_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count <
            second_frame.render_hotspot_counters.gpu_viewport_pixel_count);
    REQUIRE(second_frame.render_hotspot_counters.text_texture_upload_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.image_texture_upload_count == 0);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::PartialRedrawCopy);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_region_count >= 1);

    widget->set_image_variant(1);
    REQUIRE(app.event_loop().poll());

    const auto third_frame = window.last_frame_diagnostics();
    REQUIRE_FALSE(third_frame.had_layout);
    REQUIRE(third_frame.render_hotspot_counters.damage_region_count >= 1);
    REQUIRE(third_frame.render_hotspot_counters.gpu_draw_call_count >= 2);
    REQUIRE(third_frame.render_hotspot_counters.gpu_replayed_command_count >= 1);
    REQUIRE(third_frame.render_hotspot_counters.gpu_replayed_command_count <=
            first_frame.render_hotspot_counters.gpu_replayed_command_count);
    REQUIRE(third_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count > 0);
    REQUIRE(third_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count <
            first_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count);
    REQUIRE(third_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count <
            third_frame.render_hotspot_counters.gpu_viewport_pixel_count);
    REQUIRE(third_frame.render_hotspot_counters.text_texture_upload_count == 0);
    REQUIRE(third_frame.render_hotspot_counters.image_texture_upload_count >= 1);
    REQUIRE(third_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::PartialRedrawCopy);
    REQUIRE(third_frame.render_hotspot_counters.gpu_present_region_count >= 1);
}

TEST_CASE("Win32 D3D11 forces a full copy on the first visible dirty frame",
          "[app][render][windows]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("d3d11") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "D3D11 first dirty frame", .width = 300, .height = 220});
    auto widget = LocalizedMixedGpuWidget::create(220.0F, 150.0F);
    window.set_child(widget);

    widget->set_title("Pre-present dirty content");

    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::D3D11);

    const auto frame = window.last_frame_diagnostics();
    REQUIRE(frame.render_hotspot_counters.damage_region_count == 0);
    REQUIRE(frame.render_hotspot_counters.gpu_draw_call_count >= 3);
    REQUIRE(frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::FullRedrawCopyBack);
    REQUIRE(frame.render_hotspot_counters.gpu_present_region_count == 0);
    REQUIRE(frame.render_hotspot_counters.gpu_swapchain_copy_count == 1);
}

TEST_CASE("Win32 D3D11 merges nearby image damage slices into one textured replay",
          "[app][render][windows]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("d3d11") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "D3D11 merged image damage", .width = 280, .height = 220});
    auto widget = MultiDamageImageWidget::create(200.0F, 150.0F);
    window.set_child(widget);

    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::D3D11);

    const auto first_frame = window.last_frame_diagnostics();
    REQUIRE(first_frame.render_hotspot_counters.image_node_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.gpu_replayed_command_count >= 2);

    widget->queue_close_image_updates();
    REQUIRE(app.event_loop().poll());

    const auto second_frame = window.last_frame_diagnostics();
    REQUIRE_FALSE(second_frame.had_layout);
    REQUIRE(second_frame.render_hotspot_counters.damage_region_count >= 2);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_region_count >= 2);
    REQUIRE(second_frame.render_hotspot_counters.gpu_replayed_command_count == 3);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::PartialRedrawCopy);
}
#endif

TEST_CASE("Native Vulkan-capable surfaces expose interop handles", "[app][render]") {
#if defined(NK_HAVE_VULKAN) && (defined(__linux__) || defined(_WIN32))
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan interop", .width = 320, .height = 240});
    window.set_child(nk::Label::create("Vulkan interop"));

    window.present();
    REQUIRE(app.event_loop().poll());

    auto* surface = window.native_surface();
    REQUIRE(surface != nullptr);
    const auto support = surface->renderer_backend_support();
    REQUIRE(support.software);
#if defined(NK_HAVE_VULKAN)
    REQUIRE(support.vulkan);
#endif
    REQUIRE(surface->native_handle() != nullptr);
    REQUIRE(surface->native_display_handle() != nullptr);
#else
    SUCCEED("Native Vulkan interop handles are only exposed on Vulkan-capable desktop backends");
#endif
}

TEST_CASE("Window exposes content scale and framebuffer size from the native surface",
          "[app][render]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Scale metrics", .width = 320, .height = 240});
    window.set_child(nk::Label::create("Scale metrics"));

    window.present();
    REQUIRE(app.event_loop().poll());

    auto* surface = window.native_surface();
    REQUIRE(surface != nullptr);
    REQUIRE(window.scale_factor() == Catch::Approx(surface->scale_factor()));
    REQUIRE(window.framebuffer_size() == surface->framebuffer_size());

    const auto logical = window.size();
    const auto framebuffer = window.framebuffer_size();
    REQUIRE(framebuffer.width ==
            Catch::Approx(std::round(logical.width * window.scale_factor())).margin(1.0));
    REQUIRE(framebuffer.height ==
            Catch::Approx(std::round(logical.height * window.scale_factor())).margin(1.0));
}

#if defined(_WIN32)
TEST_CASE("Win32 DPI changes update scale factor and framebuffer metrics",
          "[app][render][windows]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "DPI change", .width = 320, .height = 240});
    window.set_child(nk::Label::create("DPI change"));

    float observed_scale_factor = 0.0F;
    auto scale_conn = window.on_scale_factor_changed().connect(
        [&](float scale_factor) { observed_scale_factor = scale_factor; });
    (void)scale_conn;

    window.present();
    REQUIRE(app.event_loop().poll());

    auto* surface = window.native_surface();
    REQUIRE(surface != nullptr);
    auto* hwnd = static_cast<HWND>(surface->native_handle());
    REQUIRE(hwnd != nullptr);

    const auto logical_size = window.size();
    const auto initial_scale_factor = window.scale_factor();
    const UINT initial_dpi = static_cast<UINT>(std::lround(initial_scale_factor * 96.0F));
    const UINT target_dpi = initial_dpi == 96U ? 144U : 96U;
    REQUIRE(target_dpi != initial_dpi);

    RECT suggested = suggested_window_rect_for_dpi(hwnd, logical_size, target_dpi);
    SendMessageW(hwnd,
                 WM_DPICHANGED,
                 MAKEWPARAM(target_dpi, target_dpi),
                 reinterpret_cast<LPARAM>(&suggested));

    REQUIRE(window.scale_factor() == Catch::Approx(static_cast<float>(target_dpi) / 96.0F));
    REQUIRE(observed_scale_factor == Catch::Approx(window.scale_factor()));

    const auto framebuffer = window.framebuffer_size();
    RECT client_rect{};
    GetClientRect(hwnd, &client_rect);
    REQUIRE(framebuffer.width == static_cast<float>(client_rect.right - client_rect.left));
    REQUIRE(framebuffer.height == static_cast<float>(client_rect.bottom - client_rect.top));

    const auto updated_logical_size = window.size();
    REQUIRE(updated_logical_size.width ==
            Catch::Approx(framebuffer.width / window.scale_factor()).margin(1.0));
    REQUIRE(updated_logical_size.height ==
            Catch::Approx(framebuffer.height / window.scale_factor()).margin(1.0));

    REQUIRE(app.event_loop().poll());
    REQUIRE(nk::has_frame_request_reason(window.last_frame_diagnostics(),
                                         nk::FrameRequestReason::ScaleFactorChanged));
}
#endif

TEST_CASE("Desktop backends can opt into the Vulkan renderer backend experimentally",
          "[app][render]") {
#if defined(NK_HAVE_VULKAN) && (defined(__linux__) || defined(_WIN32))
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("vulkan") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan renderer override", .width = 320, .height = 240});
    window.set_child(nk::Label::create("Vulkan renderer override"));

    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::Vulkan);

#else
    SUCCEED("Experimental Vulkan override is only available on Vulkan-capable desktop backends");
#endif
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

TEST_CASE("Window frame requests are not starved by continuously ready timers", "[app][render]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Frame scheduling", .width = 240, .height = 120});
    auto child = FixedWidget::create(120.0F, 40.0F);
    window.set_child(child);

    window.present();
    REQUIRE(app.event_loop().poll());
    const auto first_frame_id = window.last_frame_diagnostics().frame_id;

    int timer_ticks = 0;
    nk::CallbackHandle interval{};
    interval = app.event_loop().set_interval(
        std::chrono::milliseconds{0},
        [&] {
            ++timer_ticks;
            child->queue_redraw();
            if (timer_ticks >= 2) {
                app.event_loop().cancel(interval);
            }
        },
        "test.frame-starvation");

    REQUIRE(app.event_loop().poll());
    REQUIRE(timer_ticks >= 1);
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.last_frame_diagnostics().frame_id > first_frame_id);
    REQUIRE(nk::has_frame_request_reason(window.last_frame_diagnostics(),
                                         nk::FrameRequestReason::WidgetRedraw));
}

TEST_CASE("Window reacts to system preference changes", "[app][prefs]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("software") == 0);

    nk::Application app(0, nullptr);
    // Observation uses a native GSettings/portal path; turn it off so the test drives the signal
    // deterministically and doesn't race with the real desktop preferences.
    app.set_system_preferences_observation_enabled(false);

    nk::Window window({.title = "Prefs reactive", .width = 240, .height = 120});
    window.set_child(nk::Label::create("prefs"));
    window.present();
    REQUIRE(app.event_loop().poll());
    const auto baseline_frame_id = window.last_frame_diagnostics().frame_id;

    nk::SystemPreferences next = app.system_preferences();
    next.color_scheme = (next.color_scheme == nk::ColorScheme::Dark) ? nk::ColorScheme::Light
                                                                     : nk::ColorScheme::Dark;
    app.on_system_preferences_changed().emit(next);

    REQUIRE(app.event_loop().poll());
    REQUIRE(window.last_frame_diagnostics().frame_id > baseline_frame_id);
    REQUIRE(nk::has_frame_request_reason(window.last_frame_diagnostics(),
                                         nk::FrameRequestReason::SystemPreferencesChanged));
}

TEST_CASE("Vulkan mixed-content frames upload image and text textures", "[app][render]") {
#if defined(NK_HAVE_VULKAN) && (defined(__linux__) || defined(_WIN32))
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("vulkan") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan mixed renderer", .width = 300, .height = 220});
    auto widget = MixedGpuWidget::create(220.0F, 150.0F);
    window.set_child(widget);

    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::Vulkan);

    const auto first_frame = window.last_frame_diagnostics();
    REQUIRE(first_frame.render_hotspot_counters.text_node_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.image_node_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.text_texture_upload_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.image_texture_upload_count >= 1);

    window.request_frame();
    REQUIRE(app.event_loop().poll());

    const auto second_frame = window.last_frame_diagnostics();
    REQUIRE(second_frame.render_hotspot_counters.text_node_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.image_node_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.text_shape_cache_hit_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.text_texture_upload_count == 0);
    REQUIRE(second_frame.render_hotspot_counters.image_texture_upload_count == 0);

#else
    SUCCEED("Vulkan mixed-content path is only available on Vulkan-capable desktop backends");
#endif
}

TEST_CASE("Linux Vulkan redraws fewer GPU commands for localized widget damage", "[app][render]") {
#if defined(__linux__)
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("vulkan") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan partial redraw", .width = 420, .height = 220});

    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
    layout->set_spacing(24.0F);
    root->set_layout_manager(std::move(layout));

    auto left = PrimitiveClipWidget::create(160.0F, 120.0F);
    auto right = PrimitiveClipWidget::create(160.0F, 120.0F);
    root->append(left);
    root->append(right);

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::Vulkan);

    const auto first_frame = window.last_frame_diagnostics();
    REQUIRE(first_frame.render_hotspot_counters.damage_region_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_draw_call_count >= 6);
    REQUIRE(first_frame.render_hotspot_counters.gpu_replayed_command_count >=
            first_frame.render_hotspot_counters.gpu_draw_call_count);
    REQUIRE(first_frame.render_hotspot_counters.gpu_skipped_command_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_region_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_swapchain_copy_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_viewport_pixel_count > 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count > 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count <=
            first_frame.render_hotspot_counters.gpu_viewport_pixel_count);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::FullRedrawDirect);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_tradeoff ==
            nk::GpuPresentTradeoff::BandwidthFavored);

    left->queue_redraw();
    REQUIRE(app.event_loop().poll());

    const auto second_frame = window.last_frame_diagnostics();
    REQUIRE(second_frame.render_hotspot_counters.damage_region_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_draw_call_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_draw_call_count <
            first_frame.render_hotspot_counters.gpu_draw_call_count);
    REQUIRE(second_frame.render_hotspot_counters.gpu_replayed_command_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_replayed_command_count <
            first_frame.render_hotspot_counters.gpu_replayed_command_count);
    REQUIRE(second_frame.render_hotspot_counters.gpu_skipped_command_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_region_count <=
            second_frame.render_hotspot_counters.damage_region_count);
    REQUIRE(second_frame.render_hotspot_counters.gpu_swapchain_copy_count <= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_viewport_pixel_count > 0);
    REQUIRE(second_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count > 0);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::PartialRedrawCopy);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_tradeoff ==
            nk::GpuPresentTradeoff::DrawFavored);

#else
    SUCCEED("Vulkan partial redraw is Linux-specific");
#endif
}

TEST_CASE("macOS Metal redraws fewer GPU commands for localized widget damage", "[app][render]") {
#if defined(__APPLE__)
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("metal") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Metal partial redraw", .width = 420, .height = 220});

    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
    layout->set_spacing(24.0F);
    root->set_layout_manager(std::move(layout));

    auto left = PrimitiveClipWidget::create(160.0F, 120.0F);
    auto right = PrimitiveClipWidget::create(160.0F, 120.0F);
    root->append(left);
    root->append(right);

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::Metal);

    const auto first_frame = window.last_frame_diagnostics();
    REQUIRE(first_frame.render_hotspot_counters.damage_region_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_draw_call_count >= 6);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_region_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_viewport_pixel_count > 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::FullRedrawCopyBack);

    left->queue_redraw();
    REQUIRE(app.event_loop().poll());

    const auto second_frame = window.last_frame_diagnostics();
    REQUIRE(second_frame.render_hotspot_counters.damage_region_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_region_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_region_count <=
            second_frame.render_hotspot_counters.damage_region_count);
    REQUIRE(second_frame.render_hotspot_counters.gpu_draw_call_count >= 2);
    REQUIRE(second_frame.render_hotspot_counters.gpu_draw_call_count <
            first_frame.render_hotspot_counters.gpu_draw_call_count);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::PartialRedrawCopy);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_tradeoff ==
            nk::GpuPresentTradeoff::DrawFavored);

#else
    SUCCEED("Metal partial redraw is macOS-specific");
#endif
}

TEST_CASE("Window damage regions include previous widget bounds after movement", "[app][render]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("software") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Software previous damage", .width = 260, .height = 160});

    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
    layout->set_spacing(12.0F);
    root->set_layout_manager(std::move(layout));

    auto left = PrimitiveClipWidget::create(80.0F, 80.0F);
    auto right = PrimitiveClipWidget::create(80.0F, 80.0F);
    root->append(left);
    root->append(right);

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    const auto original = left->allocation();
    left->allocate({original.x + 120.0F, original.y, original.width, original.height});
    left->queue_redraw();
    REQUIRE(app.event_loop().poll());

    const auto frame = window.last_frame_diagnostics();
    REQUIRE_FALSE(frame.had_layout);
    REQUIRE(frame.render_hotspot_counters.damage_region_count >= 2);
    REQUIRE(frame.render_hotspot_counters.gpu_estimated_draw_pixel_count > 0);
    REQUIRE(frame.render_hotspot_counters.gpu_estimated_draw_pixel_count <
            frame.render_hotspot_counters.gpu_viewport_pixel_count);
}

TEST_CASE("Window layout reflow promotes sibling damage to full redraw", "[app][render]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("software") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Layout reflow damage", .width = 320, .height = 160});

    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
    layout->set_spacing(12.0F);
    root->set_layout_manager(std::move(layout));

    auto first = ResizableColorWidget::create(48.0F, 72.0F, nk::Color::from_rgb(26, 153, 150));
    auto second = ResizableColorWidget::create(72.0F, 72.0F, nk::Color::from_rgb(51, 89, 170));
    root->append(first);
    root->append(second);

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    const auto second_original = second->allocation();
    first->set_width(132.0F);
    REQUIRE(app.event_loop().poll());

    REQUIRE(second->allocation().x > second_original.x);
    REQUIRE(second->debug_has_previous_allocation());
    REQUIRE(second->debug_previous_allocation() == second_original);

    const auto frame = window.last_frame_diagnostics();
    REQUIRE(frame.had_layout);
    REQUIRE(has_frame_request_reason(frame, nk::FrameRequestReason::WidgetLayout));
    REQUIRE(frame.render_hotspot_counters.damage_region_count == 0);
    REQUIRE(frame.render_hotspot_counters.gpu_estimated_draw_pixel_count ==
            frame.render_hotspot_counters.gpu_viewport_pixel_count);
}

#if defined(_WIN32)
TEST_CASE("Win32 D3D11 uses partial present regions for localized redraw",
          "[app][render][windows]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("d3d11") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "D3D11 localized redraw", .width = 420, .height = 220});

    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
    layout->set_spacing(12.0F);
    root->set_layout_manager(std::move(layout));

    auto left = PrimitiveClipWidget::create(120.0F, 120.0F);
    auto right = PrimitiveClipWidget::create(120.0F, 120.0F);
    root->append(left);
    root->append(right);

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::D3D11);

    const auto first_frame = window.last_frame_diagnostics();
    REQUIRE(first_frame.render_hotspot_counters.damage_region_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_draw_call_count >= 6);
    REQUIRE(first_frame.render_hotspot_counters.gpu_replayed_command_count >= 6);
    REQUIRE(first_frame.render_hotspot_counters.gpu_skipped_command_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_region_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::FullRedrawCopyBack);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_tradeoff ==
            nk::GpuPresentTradeoff::DrawFavored);

    left->queue_redraw();
    REQUIRE(app.event_loop().poll());

    const auto second_frame = window.last_frame_diagnostics();
    REQUIRE(second_frame.render_hotspot_counters.damage_region_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_draw_call_count >= 2);
    REQUIRE(second_frame.render_hotspot_counters.gpu_draw_call_count <
            first_frame.render_hotspot_counters.gpu_draw_call_count);
    REQUIRE(second_frame.render_hotspot_counters.gpu_replayed_command_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_replayed_command_count <=
            first_frame.render_hotspot_counters.gpu_replayed_command_count);
    REQUIRE(second_frame.render_hotspot_counters.gpu_skipped_command_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_region_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_swapchain_copy_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::PartialRedrawCopy);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_tradeoff ==
            nk::GpuPresentTradeoff::BandwidthFavored);
}

TEST_CASE("Win32 D3D11 renders linear gradients on the GPU scene", "[app][render][windows]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("d3d11") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "D3D11 gradient", .width = 340, .height = 180});

    auto gradient_widget = NativeGradientGpuWidget::create(190.0F, 124.0F);
    window.set_child(gradient_widget);
    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::D3D11);

    const auto first_frame = window.last_frame_diagnostics();
    REQUIRE(first_frame.render_hotspot_counters.damage_region_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_draw_call_count >= 3);
    REQUIRE(first_frame.render_hotspot_counters.gpu_replayed_command_count >= 3);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_region_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::FullRedrawCopyBack);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_tradeoff ==
            nk::GpuPresentTradeoff::DrawFavored);

    gradient_widget->set_accent(nk::Color::from_rgb(51, 89, 170));
    REQUIRE(app.event_loop().poll());

    const auto second_frame = window.last_frame_diagnostics();
    REQUIRE(second_frame.render_hotspot_counters.damage_region_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_draw_call_count >= 2);
    REQUIRE(second_frame.render_hotspot_counters.gpu_replayed_command_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_region_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_swapchain_copy_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::PartialRedrawCopy);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_tradeoff ==
            nk::GpuPresentTradeoff::BandwidthFavored);
}

TEST_CASE("Win32 D3D11 falls back to software upload for unsupported semantic nodes",
          "[app][render][windows]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("d3d11") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "D3D11 semantic fallback", .width = 340, .height = 180});

    auto fallback_widget = UnsupportedSemanticGpuWidget::create(180.0F, 120.0F);
    window.set_child(fallback_widget);
    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::D3D11);

    const auto first_frame = window.last_frame_diagnostics();
    REQUIRE(first_frame.render_hotspot_counters.damage_region_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_draw_call_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_swapchain_copy_count == 1);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_region_count == 0);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::SoftwareUpload);
    REQUIRE(first_frame.render_hotspot_counters.gpu_present_tradeoff ==
            nk::GpuPresentTradeoff::None);

    fallback_widget->set_accent(nk::Color::from_rgb(51, 89, 170));
    REQUIRE(app.event_loop().poll());

    const auto second_frame = window.last_frame_diagnostics();
    REQUIRE(second_frame.render_hotspot_counters.damage_region_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_draw_call_count == 0);
    REQUIRE(second_frame.render_hotspot_counters.gpu_swapchain_copy_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_region_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::SoftwareUpload);
    REQUIRE(second_frame.render_hotspot_counters.gpu_present_tradeoff ==
            nk::GpuPresentTradeoff::BandwidthFavored);
}
#endif

TEST_CASE("Linux Vulkan adapts large full redraws to copy-back scene preservation",
          "[app][render]") {
#if defined(__linux__)
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("vulkan") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan full redraw strategy", .width = 520, .height = 180});

    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
    layout->set_spacing(12.0F);
    root->set_layout_manager(std::move(layout));

    root->append(PrimitiveClipWidget::create(120.0F, 120.0F));
    root->append(PrimitiveClipWidget::create(120.0F, 120.0F));
    root->append(PrimitiveClipWidget::create(120.0F, 120.0F));
    root->append(PrimitiveClipWidget::create(120.0F, 120.0F));

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::Vulkan);

    const auto frame = window.last_frame_diagnostics();
    REQUIRE(frame.render_hotspot_counters.damage_region_count == 0);
    REQUIRE(frame.render_hotspot_counters.gpu_swapchain_copy_count == 1);
    REQUIRE(frame.render_hotspot_counters.gpu_draw_call_count >= 12);
    REQUIRE(frame.render_hotspot_counters.gpu_draw_call_count < 24);
    REQUIRE(frame.render_hotspot_counters.gpu_viewport_pixel_count > 0);
    REQUIRE(frame.render_hotspot_counters.gpu_estimated_draw_pixel_count > 0);
    REQUIRE(frame.render_hotspot_counters.gpu_estimated_draw_pixel_count >=
            frame.render_hotspot_counters.gpu_viewport_pixel_count);
    REQUIRE(frame.render_hotspot_counters.gpu_present_path ==
            nk::GpuPresentPath::FullRedrawCopyBack);
    REQUIRE(frame.render_hotspot_counters.gpu_present_tradeoff ==
            nk::GpuPresentTradeoff::DrawFavored);

#else
    SUCCEED("Adaptive Vulkan full redraw strategy is Linux-specific");
#endif
}

TEST_CASE("Window close requests are notification-only and hide the window", "[app]") {
    nk::Window window({.title = "Close test", .width = 320, .height = 240});

    bool close_requested = false;
    auto conn = window.on_close_requested().connect([&close_requested] { close_requested = true; });
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

TEST_CASE("Application reports native app-menu capability explicitly", "[app][menu]") {
    nk::Application app(0, nullptr);

    const bool expected_support =
        app.has_platform_backend() ? app.platform_backend().supports_native_app_menu() : false;
    REQUIRE(app.supports_native_app_menu() == expected_support);

    app.set_native_app_menu({
        {
            .title = "File",
            .items =
                {
                    nk::NativeMenuItem::action("Quit",
                                               "file.quit",
                                               nk::NativeMenuShortcut{
                                                   .key = nk::KeyCode::Q,
                                                   .modifiers = nk::NativeMenuModifier::Super,
                                               }),
                },
        },
    });
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

TEST_CASE("ScrollArea uses precise scroll deltas without wheel-step amplification",
          "[app][scroll]") {
    nk::Window window({.title = "Precise scroll test", .width = 120, .height = 80});
    auto scroll_area = nk::ScrollArea::create();
    auto content = FixedWidget::create(200.0F, 300.0F);
    scroll_area->set_content(content);
    window.set_child(scroll_area);

    scroll_area->allocate({0.0F, 0.0F, 120.0F, 80.0F});

    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Scroll,
        .x = 40.0F,
        .y = 40.0F,
        .scroll_dx = 0.0F,
        .scroll_dy = -12.0F,
        .precise_scrolling = true,
    });

    REQUIRE(scroll_area->v_offset() == Catch::Approx(12.0F));
}

TEST_CASE("ScrollArea supports dragging the vertical scrollbar thumb", "[app][scroll]") {
    nk::Window window({.title = "Scroll drag test", .width = 120, .height = 80});
    auto scroll_area = nk::ScrollArea::create();
    scroll_area->set_h_scroll_policy(nk::ScrollPolicy::Never);
    scroll_area->set_v_scroll_policy(nk::ScrollPolicy::Always);
    auto content = FixedWidget::create(120.0F, 320.0F);
    scroll_area->set_content(content);
    window.set_child(scroll_area);

    scroll_area->allocate({0.0F, 0.0F, 120.0F, 80.0F});

    const float track_x = 108.0F;
    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Press,
        .x = track_x,
        .y = 20.0F,
        .button = 1,
    });
    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Move,
        .x = track_x,
        .y = 56.0F,
        .button = 1,
    });
    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Release,
        .x = track_x,
        .y = 56.0F,
        .button = 1,
    });

    REQUIRE(scroll_area->v_offset() > 0.0F);
}

TEST_CASE("ScrollArea positions content relative to its viewport origin", "[app][scroll]") {
    auto scroll_area = nk::ScrollArea::create();
    auto content = FixedWidget::create(200.0F, 300.0F);
    scroll_area->set_content(content);

    scroll_area->allocate({24.0F, 36.0F, 120.0F, 80.0F});
    REQUIRE(content->allocation() == nk::Rect{24.0F, 36.0F, 200.0F, 300.0F});

    scroll_area->scroll_to(18.0F, 40.0F);
    scroll_area->allocate({24.0F, 36.0F, 120.0F, 80.0F});
    REQUIRE(content->allocation() == nk::Rect{6.0F, -4.0F, 200.0F, 300.0F});
}

TEST_CASE("ScrollArea queues layout when the scroll offset changes", "[app][scroll]") {
    nk::Window window({.title = "Scroll layout test", .width = 120, .height = 80});
    auto scroll_area = nk::ScrollArea::create();
    auto content = FixedWidget::create(200.0F, 300.0F);
    scroll_area->set_content(content);
    window.set_child(scroll_area);
    scroll_area->allocate({0.0F, 0.0F, 120.0F, 80.0F});

    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Scroll,
        .x = 40.0F,
        .y = 40.0F,
        .scroll_dx = 0.0F,
        .scroll_dy = -1.0F,
    });

    REQUIRE(scroll_area->v_offset() > 0.0F);
    scroll_area->allocate({0.0F, 0.0F, 120.0F, 80.0F});
    REQUIRE(content->allocation().y < 0.0F);
}

TEST_CASE("ScrollArea constrains content width when horizontal scrolling is disabled",
          "[app][scroll]") {
    auto scroll_area = nk::ScrollArea::create();
    scroll_area->set_h_scroll_policy(nk::ScrollPolicy::Never);
    auto content = ConstraintAwareWidget::create();
    scroll_area->set_content(content);

    scroll_area->allocate({0.0F, 0.0F, 120.0F, 40.0F});

    REQUIRE(content->last_constraints().max_width == Catch::Approx(120.0F));
    REQUIRE(content->allocation().width == Catch::Approx(120.0F));
    REQUIRE(content->allocation().height == Catch::Approx(48.0F));

    scroll_area->scroll_to(0.0F, 1000.0F);
    REQUIRE(scroll_area->v_offset() == Catch::Approx(8.0F));
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

TEST_CASE("MenuBar uses measured title widths for top-level menu hit testing", "[app][menu]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Menu spacing", .width = 420, .height = 120});

    auto root = TestContainer::create();
    root->set_layout_manager(std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical));

    auto menu_bar = MeasuringMenuBar::create();
    menu_bar->add_menu({.title = "File", .items = {nk::MenuItem::action("Open", "file.open")}});
    menu_bar->add_menu({.title = "Edit", .items = {nk::MenuItem::action("Undo", "edit.undo")}});
    root->append(menu_bar);
    root->append(nk::Label::create("Body"));

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    std::string action;
    auto conn =
        menu_bar->on_action().connect([&](std::string_view value) { action = std::string(value); });
    (void)conn;

    const auto file_size = menu_bar->menu_label_size("File");
    const auto edit_size = menu_bar->menu_label_size("Edit");
    const float edit_center_x = 18.0F + file_size.width + 22.0F + (edit_size.width * 0.5F);

    REQUIRE(menu_bar->handle_mouse_event({
        .type = nk::MouseEvent::Type::Press,
        .x = edit_center_x,
        .y = 16.0F,
        .button = 1,
    }));
    REQUIRE(menu_bar->handle_mouse_event({
        .type = nk::MouseEvent::Type::Release,
        .x = edit_center_x,
        .y = 16.0F,
        .button = 1,
    }));

    REQUIRE(menu_bar->handle_mouse_event({
        .type = nk::MouseEvent::Type::Press,
        .x = edit_center_x,
        .y = 48.0F,
        .button = 1,
    }));
    REQUIRE(menu_bar->handle_mouse_event({
        .type = nk::MouseEvent::Type::Release,
        .x = edit_center_x,
        .y = 48.0F,
        .button = 1,
    }));

    REQUIRE(action == "edit.undo");
}

TEST_CASE("Window menu popups use partial redraw damage beyond the menu bar bounds",
          "[app][menu][render]") {
#if defined(_WIN32)
    SUCCEED("Win32 popup damage tracking is not hardened yet");
    return;
#endif
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("software") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Menu popup render", .width = 320, .height = 220});

    auto root = TestContainer::create();
    root->set_layout_manager(std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical));

    auto menu_bar = nk::MenuBar::create();
    menu_bar->add_menu({
        .title = "File",
        .items =
            {
                nk::MenuItem::action("Popup Action", "app.popup.action"),
            },
    });
    root->append(menu_bar);
    root->append(nk::Label::create("Body"));

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.dump_selected_frame_render_snapshot().find("Popup Action") == std::string::npos);

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

    REQUIRE(app.event_loop().poll());

    const auto history = window.debug_frame_history();
    REQUIRE(history.size() >= 2);
    const auto& frame = history.back();
    REQUIRE(has_frame_request_reason(frame, nk::FrameRequestReason::WidgetRedraw));
    REQUIRE_FALSE(frame.had_layout);
    REQUIRE(frame.render_hotspot_counters.damage_region_count >= 2);
    REQUIRE(frame.render_hotspot_counters.gpu_estimated_draw_pixel_count > 0);
    REQUIRE(frame.render_hotspot_counters.gpu_estimated_draw_pixel_count <
            frame.render_hotspot_counters.gpu_viewport_pixel_count);
    REQUIRE(window.dump_selected_frame_render_snapshot().find("Popup Action") != std::string::npos);
}

TEST_CASE("Window combo popups use partial redraw damage beyond the field bounds",
          "[app][combo][render]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("software") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Combo popup render", .width = 320, .height = 220});

    auto root = TestContainer::create();
    root->set_layout_manager(std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical));

    auto combo = nk::ComboBox::create();
    combo->set_items({"NTSC", "PAL", "Dendy", "Famicom", "NES", "VS."});
    combo->set_selected_index(0);
    root->append(combo);
    root->append(nk::Label::create("Body"));

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Press,
        .x = combo->allocation().x + 18.0F,
        .y = combo->allocation().y + (combo->allocation().height * 0.5F),
        .button = 1,
    });
    window.dispatch_mouse_event({
        .type = nk::MouseEvent::Type::Release,
        .x = combo->allocation().x + 18.0F,
        .y = combo->allocation().y + (combo->allocation().height * 0.5F),
        .button = 1,
    });

    REQUIRE(app.event_loop().poll());

    const auto history = window.debug_frame_history();
    REQUIRE(history.size() >= 2);
    const auto& frame = history.back();
    REQUIRE(has_frame_request_reason(frame, nk::FrameRequestReason::WidgetRedraw));
    REQUIRE_FALSE(frame.had_layout);
    REQUIRE(frame.render_hotspot_counters.damage_region_count >= 2);
    REQUIRE(frame.render_hotspot_counters.gpu_estimated_draw_pixel_count > 0);
    REQUIRE(frame.render_hotspot_counters.gpu_estimated_draw_pixel_count <
            frame.render_hotspot_counters.gpu_viewport_pixel_count);
    REQUIRE(window.dump_selected_frame_render_snapshot().find("Famicom") != std::string::npos);
}

TEST_CASE("ComboBox popup scrolls to keep the highlighted item visible with many items",
          "[app][combo]") {
    auto combo = nk::ComboBox::create();
    std::vector<std::string> items;
    for (int i = 0; i < 20; ++i) {
        items.push_back("Option " + std::to_string(i));
    }
    combo->set_items(items);
    combo->allocate({0.0F, 0.0F, 200.0F, 36.0F});

    // Opening via Down selects index 0 and scrolls to the top.
    combo->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Down});

    // Paging all the way down with End must keep the highlight reachable even though only the
    // first 6 items were visible before the scroll. Without the scroll-offset fix, the popup
    // rendered indices 0–5 only and index 19 was unselectable.
    combo->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::End});
    combo->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return});
    REQUIRE(combo->selected_index() == 19);

    // And Home + Return should pick index 0.
    combo->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Down});
    combo->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Home});
    combo->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return});
    REQUIRE(combo->selected_index() == 0);
}

TEST_CASE("ComboBox resets popup state when the item list is replaced", "[app][combo]") {
    auto combo = nk::ComboBox::create();
    combo->set_items({"Alpha", "Beta", "Gamma"});
    combo->allocate({0.0F, 0.0F, 200.0F, 36.0F});
    combo->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Down});

    // Replacing the item list while the popup is open must also clear the selection and not
    // leave a stale highlighted_index pointing into the old list.
    combo->set_items({"One", "Two"});
    REQUIRE(combo->selected_index() == -1);

    // Re-opening and pressing Down once should now highlight index 0 against the new list.
    combo->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Down});
    combo->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return});
    REQUIRE(combo->selected_index() == 0);
}

TEST_CASE("ImageView measure clamps the natural size to parent constraints", "[app][image]") {
    auto image = nk::ImageView::create();
    std::vector<uint32_t> pixels(800U * 600U, 0xFFAABBCCU);
    image->update_pixel_buffer(pixels.data(), 800, 600);
    REQUIRE(image->source_width() == 800);
    REQUIRE(image->source_height() == 600);

    const auto unbounded = image->measure({});
    REQUIRE(unbounded.natural_width == Catch::Approx(800.0F));
    REQUIRE(unbounded.natural_height == Catch::Approx(600.0F));

    nk::Constraints tight;
    tight.max_width = 240.0F;
    tight.max_height = 180.0F;
    const auto clamped = image->measure(tight);
    REQUIRE(clamped.natural_width == Catch::Approx(240.0F));
    REQUIRE(clamped.natural_height == Catch::Approx(180.0F));
}

TEST_CASE("StatusBar advertises the accessible status role", "[app][status]") {
    auto status = nk::StatusBar::create();
    status->set_segments({"Ready", "Saved"});
    REQUIRE(status->ensure_accessible().role() == nk::AccessibleRole::Status);
    REQUIRE(status->segment_count() == 2);
    REQUIRE(status->segment(1) == "Saved");
}

TEST_CASE("SegmentedControl selects segments through API, mouse, and keyboard", "[app][widgets]") {
    auto control = nk::SegmentedControl::create();
    control->set_segments({"Input", "Video", "Audio"});

    REQUIRE(control->segment_count() == 3);
    REQUIRE(control->selected_index() == 0);
    REQUIRE(control->selected_text() == "Input");
    REQUIRE(control->measure({}).natural_width > 0.0F);

    int selected = -1;
    [[maybe_unused]] auto connection =
        control->on_selection_changed().connect([&selected](int index) { selected = index; });

    control->set_selected_index(2);
    REQUIRE(control->selected_index() == 2);
    REQUIRE(control->selected_text() == "Audio");
    REQUIRE(selected == 2);

    control->allocate({0.0F, 0.0F, 300.0F, 36.0F});
    control->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 150.0F, .y = 18.0F, .button = 1});
    control->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 150.0F, .y = 18.0F, .button = 1});
    REQUIRE(control->selected_index() == 1);

    control->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::End});
    REQUIRE(control->selected_index() == 2);

    control->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left});
    REQUIRE(control->selected_index() == 1);

    control->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Home});
    REQUIRE(control->selected_index() == 0);
}

TEST_CASE("Label, ImageView, and TextField queue localized redraw regions for content updates",
          "[app][widgets][render]") {
    auto label = nk::Label::create("One");
    label->allocate({10.0F, 10.0F, 220.0F, 40.0F});
    label->set_text("Two Words");

    const auto label_damage = label->debug_damage_regions();
    REQUIRE(label_damage.size() == 1);
    REQUIRE(label_damage.front().x >= label->allocation().x);
    REQUIRE(label_damage.front().y >= label->allocation().y);
    REQUIRE(label_damage.front().width < label->allocation().width);
    REQUIRE(label_damage.front().height < label->allocation().height);

    static constexpr std::array<uint32_t, 4> kImagePixels = {
        0xFF0C6B68,
        0xFF149E98,
        0xFF3359AA,
        0xFF8F229F,
    };

    auto image = nk::ImageView::create();
    image->allocate({0.0F, 0.0F, 200.0F, 160.0F});
    image->update_pixel_buffer(kImagePixels.data(), 4, 1);

    const auto image_damage = image->debug_damage_regions();
    REQUIRE(image_damage.size() == 1);
    REQUIRE(image_damage.front().x > image->allocation().x);
    REQUIRE(image_damage.front().y > image->allocation().y);
    REQUIRE(image_damage.front().width < image->allocation().width);
    REQUIRE(image_damage.front().height < image->allocation().height);

    auto field = nk::TextField::create("alpha beta");
    field->allocate({0.0F, 0.0F, 220.0F, 36.0F});
    field->select_all();

    const auto field_damage = field->debug_damage_regions();
    REQUIRE(field_damage.size() == 1);
    REQUIRE(field_damage.front().x > field->allocation().x);
    REQUIRE(field_damage.front().y > field->allocation().y);
    REQUIRE(field_damage.front().width < field->allocation().width);
    REQUIRE(field_damage.front().height < field->allocation().height);
}

TEST_CASE("StatusBar, MenuBar, ComboBox, and ListView queue localized redraw regions",
          "[app][widgets][render]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Widget localized redraw", .width = 520, .height = 320});

    auto root = TestContainer::create();
    root->set_layout_manager(std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical));

    auto status = nk::StatusBar::create();
    status->set_segments({"Windows ready", "10 items", "Counter 0"});
    root->append(status);

    auto menu_bar = nk::MenuBar::create();
    menu_bar->add_menu({.title = "File", .items = {nk::MenuItem::action("Open", "file.open")}});
    menu_bar->add_menu({.title = "Edit", .items = {nk::MenuItem::action("Undo", "edit.undo")}});
    root->append(menu_bar);

    auto combo = nk::ComboBox::create();
    combo->set_items({"One", "Two", "Three"});
    combo->set_selected_index(0);
    root->append(combo);

    auto model = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{"Alpha", "Beta", "Gamma", "Delta", "Epsilon"});
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    selection->set_current_row(0);
    selection->select(0);
    auto list_view = nk::ListView::create();
    list_view->set_model(model);
    list_view->set_selection_model(selection);
    root->append(list_view);

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    status->set_segment(1, "128 items");
    const auto status_damage = status->debug_damage_regions();
    REQUIRE(status_damage.size() == 1);
    REQUIRE(status_damage.front().x > status->allocation().x);
    REQUIRE(status_damage.front().width < status->allocation().width);

    REQUIRE(menu_bar->handle_mouse_event({
        .type = nk::MouseEvent::Type::Press,
        .x = menu_bar->allocation().x + 18.0F,
        .y = menu_bar->allocation().y + (menu_bar->allocation().height * 0.5F),
        .button = 1,
    }));
    REQUIRE(menu_bar->handle_mouse_event({
        .type = nk::MouseEvent::Type::Release,
        .x = menu_bar->allocation().x + 18.0F,
        .y = menu_bar->allocation().y + (menu_bar->allocation().height * 0.5F),
        .button = 1,
    }));
    const auto menu_damage = menu_bar->debug_damage_regions();
    REQUIRE(std::any_of(menu_damage.begin(), menu_damage.end(), [&](const nk::Rect& region) {
        return region.y == Catch::Approx(menu_bar->allocation().y) &&
               region.width < menu_bar->allocation().width;
    }));
    REQUIRE(std::any_of(menu_damage.begin(), menu_damage.end(), [&](const nk::Rect& region) {
        return region.y >= menu_bar->allocation().bottom();
    }));

    combo->set_selected_index(2);
    const auto combo_damage = combo->debug_damage_regions();
    REQUIRE(combo_damage.size() == 1);
    REQUIRE(combo_damage.front().x > combo->allocation().x);
    REQUIRE(combo_damage.front().width < combo->allocation().width);
    REQUIRE(combo_damage.front().height < combo->allocation().height);

    selection->set_current_row(2);
    selection->select(2);
    const auto list_damage = list_view->debug_damage_regions();
    REQUIRE(list_damage.size() >= 2);
    REQUIRE(std::all_of(list_damage.begin(), list_damage.end(), [&](const nk::Rect& region) {
        return region.x >= list_view->allocation().x &&
               region.width < list_view->allocation().width &&
               region.height < list_view->allocation().height;
    }));
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

TEST_CASE("Dialog minimum width and sheet presentation style affect rendered geometry",
          "[app][dialog][render]") {
#if defined(_WIN32)
    SUCCEED("Win32 dialog snapshot geometry is not hardened yet");
    return;
#endif
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("software") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Dialog layout", .width = 480, .height = 320});
    auto root = TestContainer::create();
    root->append(nk::Label::create("Body"));
    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    auto centered = nk::Dialog::create("Centered Dialog", "Proceed?");
    centered->set_minimum_panel_width(420.0F);
    centered->present(window);
    REQUIRE(app.event_loop().poll());

    const auto centered_snapshot_result =
        nk::parse_render_snapshot_json(window.dump_selected_frame_render_snapshot_json());
    REQUIRE(centered_snapshot_result);
    const auto* centered_title =
        find_render_snapshot_node(centered_snapshot_result.value(), "Text", "Centered Dialog");
    REQUIRE(centered_title != nullptr);
    const float centered_panel_width = widest_render_snapshot_kind_containing(
        centered_snapshot_result.value(),
        {centered_title->bounds.x + 1.0F, centered_title->bounds.y + 1.0F},
        "RoundedRect");
    REQUIRE(centered_panel_width >= 420.0F);
    const auto centered_damage = centered->debug_damage_regions();
    REQUIRE(centered_damage.size() == 1);
    REQUIRE(centered_damage.front().width < window.size().width);
    REQUIRE(centered_damage.front().height < window.size().height);

    centered->close();
    REQUIRE(app.event_loop().poll());

    auto sheet = nk::Dialog::create("Sheet Dialog", "Proceed?");
    sheet->set_minimum_panel_width(420.0F);
    sheet->set_presentation_style(nk::DialogPresentationStyle::Sheet);
    sheet->present(window);
    REQUIRE(app.event_loop().poll());

    const auto sheet_snapshot_result =
        nk::parse_render_snapshot_json(window.dump_selected_frame_render_snapshot_json());
    REQUIRE(sheet_snapshot_result);
    const auto* sheet_title =
        find_render_snapshot_node(sheet_snapshot_result.value(), "Text", "Sheet Dialog");
    REQUIRE(sheet_title != nullptr);
    const float sheet_panel_width = widest_render_snapshot_kind_containing(
        sheet_snapshot_result.value(),
        {sheet_title->bounds.x + 1.0F, sheet_title->bounds.y + 1.0F},
        "RoundedRect");
    REQUIRE(sheet_panel_width >= 420.0F);
    REQUIRE(sheet_title->bounds.y + 40.0F < centered_title->bounds.y);
    const auto sheet_damage = sheet->debug_damage_regions();
    REQUIRE(sheet_damage.size() == 1);
    REQUIRE(sheet_damage.front().width < window.size().width);
    REQUIRE(sheet_damage.front().height < window.size().height);
}

TEST_CASE("Dialog overlay layout changes promote damage to full redraw", "[app][dialog][render]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("software") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Dialog damage", .width = 320, .height = 240});
    auto root = TestContainer::create();
    root->append(nk::Label::create("Body"));
    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    auto dialog = nk::Dialog::create("Confirm", "Proceed?");
    dialog->add_button("Cancel", nk::DialogResponse::Cancel);
    dialog->present(window);
    REQUIRE(app.event_loop().poll());

    const auto present_frame = window.last_frame_diagnostics();
    REQUIRE(has_frame_request_reason(present_frame, nk::FrameRequestReason::OverlayChanged));
    REQUIRE(present_frame.had_layout);
    REQUIRE(present_frame.render_hotspot_counters.damage_region_count == 0);
    REQUIRE(present_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count ==
            present_frame.render_hotspot_counters.gpu_viewport_pixel_count);

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Escape,
    });
    REQUIRE(app.event_loop().poll());

    const auto dismiss_frame = window.last_frame_diagnostics();
    REQUIRE(has_frame_request_reason(dismiss_frame, nk::FrameRequestReason::OverlayChanged));
    REQUIRE(dismiss_frame.had_layout);
    REQUIRE(dismiss_frame.render_hotspot_counters.damage_region_count == 0);
    REQUIRE(dismiss_frame.render_hotspot_counters.gpu_estimated_draw_pixel_count ==
            dismiss_frame.render_hotspot_counters.gpu_viewport_pixel_count);
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

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::A});
    REQUIRE(window.is_key_pressed(nk::KeyCode::A));
    window.dispatch_key_event(
        {.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::A, .is_repeat = true});
    REQUIRE(window.is_key_pressed(nk::KeyCode::A));
    window.dispatch_window_event({.type = nk::WindowEvent::Type::FocusOut});
    REQUIRE_FALSE(window.is_key_pressed(nk::KeyCode::A));

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
#if defined(_WIN32)
    SUCCEED("Win32 frame-history debug export is not hardened yet");
    return;
#endif
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Trace", .width = 240, .height = 120});
    app.event_loop().clear_debug_trace_events();

    auto root = TestContainer::create();
    root->set_debug_name("trace-root");
    root->set_layout_manager(std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical));
    auto child = nk::Label::create("Trace");
    child->set_debug_name("trace-label");
    root->append(child);

    window.set_child(root);
    window.set_debug_overlay_flags(nk::DebugOverlayFlags::FrameHud);
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
    const auto selected_widget_info = window.debug_selected_widget_info();
    REQUIRE(selected_widget_info.hotspot_counters.measure_count >= 1);
    REQUIRE(selected_widget_info.hotspot_counters.allocate_count >= 1);
    REQUIRE(selected_widget_info.hotspot_counters.snapshot_count >= 1);
    REQUIRE(selected_widget_info.has_last_measure);
    REQUIRE(selected_widget_info.horizontal_size_policy == "Preferred");
    REQUIRE(selected_widget_info.vertical_size_policy == "Preferred");
    REQUIRE(selected_widget_info.accessible_role == "label");
    REQUIRE(selected_widget_info.accessible_name == "Trace");
    REQUIRE(selected_widget_info.tree_path == std::vector<std::size_t>{0});
    REQUIRE(selected_widget_info.accessible_actions.empty());
    REQUIRE_FALSE(selected_widget_info.accessible_hidden);
    REQUIRE_FALSE(selected_widget_info.focused);
    REQUIRE_FALSE(selected_widget_info.pending_redraw);
    REQUIRE_FALSE(selected_widget_info.pending_layout);
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

    const auto selected_frame_summary = window.dump_selected_frame_summary();
    REQUIRE(selected_frame_summary.find("Frame ") != std::string::npos);
    REQUIRE(selected_frame_summary.find("render nodes:") != std::string::npos);

    const auto selected_render_details = window.dump_selected_render_node_details();
    REQUIRE(selected_render_details.find("render node:") != std::string::npos);
    REQUIRE(selected_render_details.find(selected_render.kind) != std::string::npos);

    const auto selected_widget_details = window.dump_selected_widget_details();
    REQUIRE(selected_widget_details.find("trace-label") != std::string::npos);
    REQUIRE(selected_widget_details.find("measure ") != std::string::npos);
    REQUIRE(selected_widget_details.find("size: h Preferred") != std::string::npos);
    REQUIRE(selected_widget_details.find("focus owner no") != std::string::npos);
    REQUIRE(selected_widget_details.find("invalidation: redraw clean  layout clean") !=
            std::string::npos);
    REQUIRE(selected_widget_details.find("a11y role label") != std::string::npos);
    REQUIRE(selected_widget_details.find("constraints: min") != std::string::npos);
    REQUIRE(selected_widget_details.find("request: min") != std::string::npos);

    const auto selected_widget_json = window.dump_selected_widget_details_json();
    REQUIRE(selected_widget_json.find("\"format\":\"nk-widget-debug-v1\"") != std::string::npos);
    REQUIRE(selected_widget_json.find("\"debug_name\":\"trace-label\"") != std::string::npos);
    REQUIRE(selected_widget_json.find("\"accessible_role\":\"label\"") != std::string::npos);
    const auto parsed_selected_widget = nk::parse_widget_debug_json(selected_widget_json);
    REQUIRE(parsed_selected_widget);
    REQUIRE(parsed_selected_widget->debug_name == "trace-label");
    REQUIRE(parsed_selected_widget->tree_path == std::vector<std::size_t>{0});
    REQUIRE(parsed_selected_widget->accessible_role == "label");
    REQUIRE(parsed_selected_widget->accessible_name == "Trace");

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::W,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(app.clipboard_text().find("trace-label") != std::string::npos);

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::R,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(app.clipboard_text().find("render node:") != std::string::npos);

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::F,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(app.clipboard_text().find("Frame ") != std::string::npos);

    const auto widget_details_path =
        std::filesystem::temp_directory_path() / "nodalkit_widget_details.txt";
    REQUIRE(window.save_selected_widget_details_file(widget_details_path.string()));
    {
        std::ifstream widget_in(widget_details_path);
        REQUIRE(widget_in.is_open());
        std::stringstream widget_buffer;
        widget_buffer << widget_in.rdbuf();
        REQUIRE(widget_buffer.str().find("trace-label") != std::string::npos);
    }
    std::error_code remove_widget_details_error;
    std::filesystem::remove(widget_details_path, remove_widget_details_error);

    const auto widget_details_json_path =
        std::filesystem::temp_directory_path() / "nodalkit_widget_details.json";
    REQUIRE(window.save_selected_widget_details_json_file(widget_details_json_path.string()));
    const auto loaded_selected_widget =
        nk::load_widget_debug_json_file(widget_details_json_path.string());
    REQUIRE(loaded_selected_widget);
    REQUIRE(loaded_selected_widget->debug_name == "trace-label");
    std::error_code remove_widget_details_json_error;
    std::filesystem::remove(widget_details_json_path, remove_widget_details_json_error);

    const auto render_details_path =
        std::filesystem::temp_directory_path() / "nodalkit_render_details.txt";
    REQUIRE(window.save_selected_render_node_details_file(render_details_path.string()));
    {
        std::ifstream render_in(render_details_path);
        REQUIRE(render_in.is_open());
        std::stringstream render_buffer;
        render_buffer << render_in.rdbuf();
        REQUIRE(render_buffer.str().find("render node:") != std::string::npos);
    }
    std::error_code remove_render_details_error;
    std::filesystem::remove(render_details_path, remove_render_details_error);

    const auto frame_summary_path =
        std::filesystem::temp_directory_path() / "nodalkit_frame_summary.txt";
    REQUIRE(window.save_selected_frame_summary_file(frame_summary_path.string()));
    {
        std::ifstream frame_in(frame_summary_path);
        REQUIRE(frame_in.is_open());
        std::stringstream frame_buffer;
        frame_buffer << frame_in.rdbuf();
        REQUIRE(frame_buffer.str().find("Frame ") != std::string::npos);
    }
    std::error_code remove_frame_summary_error;
    std::filesystem::remove(frame_summary_path, remove_frame_summary_error);

    {
        // Force the bundle_window onto the software backend so the test does not spin up a second
        // VkInstance alongside the main window's Vulkan renderer. Some drivers (observed on
        // NVIDIA 595.58.03) crash in vkAcquireNextImageKHR on the first VkInstance after a
        // sibling VkInstance is created, presented, and destroyed in the same process. The main
        // window keeps its normal (Vulkan-capable) backend, and the screenshot path in
        // save_debug_screenshot_ppm_file already uses a software renderer internally regardless.
        // Tracked by task #19 for a long-term shared-instance fix.
        ScopedTestEnvVar bundle_renderer_backend("NK_RENDERER_BACKEND");
        REQUIRE(bundle_renderer_backend.set("software") == 0);

        nk::Window bundle_window({.title = "Bundle export", .width = 320, .height = 180});
        auto bundle_root = TestContainer::create();
        auto bundle_child = FixedWidget::create(120.0F, 48.0F);
        bundle_child->set_debug_name("bundle-child");
        bundle_root->append(bundle_child);
        bundle_window.set_child(bundle_root);
        bundle_window.present();
        REQUIRE(app.event_loop().poll());
        bundle_window.set_debug_selected_widget(bundle_child.get());
        REQUIRE(app.event_loop().poll());

        const auto bundle_dir =
            std::filesystem::temp_directory_path() / "nodalkit_debug_bundle_export";
        std::error_code cleanup_error;
        std::filesystem::remove_all(bundle_dir, cleanup_error);

        REQUIRE(bundle_window.save_debug_bundle(bundle_dir.string()));
        REQUIRE(std::filesystem::exists(bundle_dir / "manifest.json"));
        REQUIRE(std::filesystem::exists(bundle_dir / "widget_tree.txt"));
        REQUIRE(std::filesystem::exists(bundle_dir / "widget_tree.json"));
        REQUIRE(std::filesystem::exists(bundle_dir / "frame_trace.json"));
        REQUIRE(std::filesystem::exists(bundle_dir / "frame_diagnostics.json"));
        REQUIRE(std::filesystem::exists(bundle_dir / "render_snapshot.json"));
        REQUIRE(std::filesystem::exists(bundle_dir / "selected_frame_summary.txt"));
        REQUIRE(std::filesystem::exists(bundle_dir / "selected_widget.json"));
        REQUIRE(std::filesystem::exists(bundle_dir / "screenshot.ppm"));

        {
            std::ifstream manifest_in(bundle_dir / "manifest.json");
            REQUIRE(manifest_in.is_open());
            std::stringstream manifest_buffer;
            manifest_buffer << manifest_in.rdbuf();
            REQUIRE(manifest_buffer.str().find("nk-debug-bundle-v1") != std::string::npos);
            REQUIRE(manifest_buffer.str().find("screenshot.ppm") != std::string::npos);
        }
        {
            std::ifstream widget_tree_in(bundle_dir / "widget_tree.txt");
            REQUIRE(widget_tree_in.is_open());
            std::stringstream widget_tree_buffer;
            widget_tree_buffer << widget_tree_in.rdbuf();
            REQUIRE(widget_tree_buffer.str().find("bundle-child") != std::string::npos);
        }
        {
            const auto widget_tree_json =
                nk::load_widget_debug_json_file((bundle_dir / "widget_tree.json").string());
            REQUIRE(widget_tree_json);
            REQUIRE(widget_tree_json->type_name == "Window");
        }
        {
            std::ifstream render_snapshot_in(bundle_dir / "render_snapshot.json");
            REQUIRE(render_snapshot_in.is_open());
            std::stringstream render_snapshot_buffer;
            render_snapshot_buffer << render_snapshot_in.rdbuf();
            REQUIRE(render_snapshot_buffer.str().find("\"kind\":\"Container\"") !=
                    std::string::npos);
        }
        {
            const auto selected_widget_json =
                nk::load_widget_debug_json_file((bundle_dir / "selected_widget.json").string());
            REQUIRE(selected_widget_json);
            REQUIRE(selected_widget_json->debug_name == "bundle-child");
        }
        {
            std::ifstream screenshot_in(bundle_dir / "screenshot.ppm", std::ios::binary);
            REQUIRE(screenshot_in.is_open());
            std::string ppm_header;
            std::getline(screenshot_in, ppm_header);
            REQUIRE(ppm_header == "P6");
        }

        std::filesystem::remove_all(bundle_dir, cleanup_error);
    }

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

    bool frame_requested_from_post = false;
    app.event_loop().post([&] {
        frame_requested_from_post = true;
        child->queue_redraw();
    });
    REQUIRE(app.event_loop().poll());
    REQUIRE(frame_requested_from_post);

    bool timeout_ran = false;
    bool idle_ran = false;
    (void)app.event_loop().set_timeout(std::chrono::milliseconds{0}, [&] { timeout_ran = true; });
    (void)app.event_loop().add_idle([&] { idle_ran = true; });
    REQUIRE(app.event_loop().poll());
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

    const auto updated_history = window.debug_frame_history();
    const auto redraw_frame_it = std::find_if(
        updated_history.rbegin(), updated_history.rend(), [](const nk::FrameDiagnostics& frame) {
            return nk::has_frame_request_reason(frame, nk::FrameRequestReason::WidgetRedraw);
        });
    REQUIRE(redraw_frame_it != updated_history.rend());
    const auto redraw_index =
        static_cast<std::size_t>(std::distance(redraw_frame_it, updated_history.rend()) - 1);
    const auto latest_index = updated_history.size() - 1;
    for (std::size_t step = latest_index; step > redraw_index; --step) {
        window.dispatch_key_event({
            .type = nk::KeyEvent::Type::Press,
            .key = nk::KeyCode::Left,
        });
    }

    for (std::size_t step = redraw_index; step < latest_index; ++step) {
        window.dispatch_key_event({
            .type = nk::KeyEvent::Type::Press,
            .key = nk::KeyCode::Right,
        });
    }

    auto selected_runtime_events = window.debug_selected_frame_runtime_events();
    bool found_posted_task =
        std::any_of(selected_runtime_events.begin(),
                    selected_runtime_events.end(),
                    [](const nk::TraceEvent& event) { return event.name == "posted-task"; });
    for (std::size_t step = 0; !found_posted_task && step < updated_history.size(); ++step) {
        window.dispatch_key_event({
            .type = nk::KeyEvent::Type::Press,
            .key = nk::KeyCode::Left,
        });
        selected_runtime_events = window.debug_selected_frame_runtime_events();
        found_posted_task =
            std::any_of(selected_runtime_events.begin(),
                        selected_runtime_events.end(),
                        [](const nk::TraceEvent& event) { return event.name == "posted-task"; });
    }
    if (found_posted_task) {
        REQUIRE_FALSE(selected_runtime_events.empty());
        REQUIRE(
            std::any_of(selected_runtime_events.begin(),
                        selected_runtime_events.end(),
                        [](const nk::TraceEvent& event) { return event.name == "posted-task"; }));
        REQUIRE(std::any_of(selected_runtime_events.begin(),
                            selected_runtime_events.end(),
                            [](const nk::TraceEvent& event) {
                                return event.source_label == "window.request-frame";
                            }));
    }

    const auto trace = window.dump_frame_trace_json();
    REQUIRE(trace.find("\"traceEvents\"") != std::string::npos);
    REQUIRE(trace.find("\"name\":\"widget-redraw\"") != std::string::npos);
    REQUIRE(trace.find("\"name\":\"layout\"") != std::string::npos);
    REQUIRE(trace.find("\"name\":\"frame\"") != std::string::npos);
    REQUIRE(trace.find("\"name\":\"posted-task\"") != std::string::npos);
    REQUIRE(trace.find("\"present_path\":\"") != std::string::npos);
    REQUIRE(trace.find("\"present_tradeoff\":\"") != std::string::npos);
    REQUIRE(trace.find("\"gpu_viewport_pixels\":") != std::string::npos);
    REQUIRE(trace.find("\"gpu_draw_pixels\":") != std::string::npos);
}

TEST_CASE("Render snapshots support fixture import and file round-trip", "[app][debug]") {
    REQUIRE(nk::render_snapshot_artifact_format() == "nk-render-snapshot-v1");
    REQUIRE(nk::frame_diagnostics_artifact_format() == "nk-frame-diagnostics-v1");
    REQUIRE(nk::frame_diagnostics_trace_export_format() == "chrome-trace-event-array-v1");

    const auto fixture_path = test_fixture_path("render_snapshot_fixture.json");
    const auto fixture_snapshot = nk::load_render_snapshot_json_file(fixture_path.string());
    REQUIRE(fixture_snapshot);
    REQUIRE(fixture_snapshot->kind == "Container");
    REQUIRE(fixture_snapshot->source_widget_label == "fixture-window");
    REQUIRE(fixture_snapshot->children.size() == 1);
    REQUIRE(fixture_snapshot->children.front().kind == "Text");
    REQUIRE(fixture_snapshot->children.front().source_widget_path ==
            std::vector<std::size_t>{0, 0});

    const auto fixture_dump = nk::format_render_snapshot_tree(*fixture_snapshot);
    REQUIRE(fixture_dump.find("fixture-label") != std::string::npos);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Snapshot", .width = 240, .height = 120});

    auto root = TestContainer::create();
    root->set_debug_name("snapshot-root");
    auto label = nk::Label::create("Snapshot");
    label->set_debug_name("snapshot-label");
    root->append(label);

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    const auto selected_snapshot = window.debug_selected_frame_render_snapshot();
    REQUIRE(selected_snapshot.kind == "Container");
    REQUIRE(selected_snapshot != *fixture_snapshot);

    const auto parsed_dump =
        nk::parse_render_snapshot_json(window.dump_selected_frame_render_snapshot_json());
    REQUIRE(parsed_dump);
    REQUIRE(*parsed_dump == selected_snapshot);

    const auto unsupported_format = nk::parse_render_snapshot_json(
        R"({"format":"nk-render-snapshot-v999","root":{"kind":"Container","bounds":{"x":0,"y":0,"width":1,"height":1},"children":[]}})");
    REQUIRE_FALSE(unsupported_format);
    REQUIRE(unsupported_format.error().find("unsupported render snapshot format") !=
            std::string::npos);

    const auto temp_path =
        std::filesystem::temp_directory_path() / "nodalkit_render_snapshot_roundtrip.json";
    const auto save_result =
        nk::save_render_snapshot_json_file(selected_snapshot, temp_path.string());
    REQUIRE(save_result);

    const auto loaded_roundtrip = nk::load_render_snapshot_json_file(temp_path.string());
    REQUIRE(loaded_roundtrip);
    REQUIRE(*loaded_roundtrip == selected_snapshot);
    REQUIRE(nk::count_render_snapshot_nodes(*loaded_roundtrip) ==
            nk::count_render_snapshot_nodes(selected_snapshot));

    std::error_code remove_error;
    std::filesystem::remove(temp_path, remove_error);
}

TEST_CASE("Frame diagnostics capture text, image, and model-view hotspot counters",
          "[app][debug][perf]") {
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

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Hotspots", .width = 360, .height = 320});

    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
    layout->set_spacing(8.0F);
    root->set_layout_manager(std::move(layout));

    auto label = nk::Label::create("Hotspot text");
    label->set_debug_name("hotspot-label");

    auto image = nk::ImageView::create();
    image->set_debug_name("hotspot-image");
    image->update_pixel_buffer(kPixels.data(), 4, 4);

    auto model = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{"Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot"});
    auto list_view = nk::ListView::create();
    list_view->set_debug_name("hotspot-list");
    list_view->set_model(model);
    list_view->set_item_factory(
        [model](std::size_t row) { return nk::Label::create(model->display_text(row)); });

    root->append(label);
    root->append(image);
    root->append(list_view);

    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    const auto first_frame = window.last_frame_diagnostics();
    REQUIRE(first_frame.widget_hotspot_totals.text_measure_count >= 1);
    REQUIRE(first_frame.widget_hotspot_totals.image_snapshot_count >= 1);
    REQUIRE(first_frame.widget_hotspot_totals.model_sync_count >= 1);
    REQUIRE(first_frame.widget_hotspot_totals.model_row_materialize_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.text_node_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.text_shape_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.image_node_count >= 1);
    REQUIRE(first_frame.render_hotspot_counters.image_source_pixel_count >= 16);

    window.set_debug_selected_widget(list_view.get());
    const auto selected_list = window.debug_selected_widget_info();
    REQUIRE(selected_list.hotspot_counters.model_sync_count >= 1);
    REQUIRE(selected_list.hotspot_counters.model_row_materialize_count >= 1);

    window.request_frame();
    REQUIRE(app.event_loop().poll());

    const auto second_frame = window.last_frame_diagnostics();
    REQUIRE(second_frame.render_hotspot_counters.text_node_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.text_shape_cache_hit_count >= 1);
    REQUIRE(second_frame.render_hotspot_counters.image_node_count >= 1);
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

TEST_CASE("Window cycles inspector presentation modes and only docked shrinks content area",
          "[app][debug]") {
    nk::Application app(0, nullptr);
    app.set_system_preferences_observation_enabled(false);

    nk::Window window({.title = "Docked inspector", .width = 720, .height = 420});
    auto root = TestContainer::create();
    auto child = FixedWidget::create(160.0F, 120.0F);
    root->append(child);
    window.set_child(root);

    window.present();
    REQUIRE(app.event_loop().poll());
    const auto viewport_size = window.size();
    REQUIRE(root->allocation().x == Catch::Approx(0.0F));
    REQUIRE(root->allocation().width == Catch::Approx(viewport_size.width));
    REQUIRE(window.debug_inspector_presentation() == nk::DebugInspectorPresentation::Overlay);

    window.set_debug_overlay_flags(nk::DebugOverlayFlags::InspectorPanel);
    window.set_debug_inspector_presentation(nk::DebugInspectorPresentation::DockedRight);
    REQUIRE(app.event_loop().poll());
    REQUIRE(root->allocation().width < viewport_size.width);
    REQUIRE(window.debug_inspector_presentation() == nk::DebugInspectorPresentation::DockedRight);

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::D,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(window.debug_inspector_presentation() == nk::DebugInspectorPresentation::Detached);
    REQUIRE(app.event_loop().poll());
    REQUIRE(root->allocation().width == Catch::Approx(viewport_size.width));

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::D,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(window.debug_inspector_presentation() == nk::DebugInspectorPresentation::Overlay);
    REQUIRE(app.event_loop().poll());
    REQUIRE(root->allocation().width == Catch::Approx(viewport_size.width));

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::D,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(window.debug_inspector_presentation() == nk::DebugInspectorPresentation::DockedRight);
    REQUIRE(app.event_loop().poll());
    REQUIRE(root->allocation().width < viewport_size.width);
}

TEST_CASE("Window widget-tree filter matches debug name, class, and type", "[app][debug]") {
    nk::Window window({.title = "Widget filter", .width = 320, .height = 160});
    auto root = TestContainer::create();
    root->set_layout_manager(std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal));

    auto first = FocusProbeWidget::create(80.0F, 40.0F);
    first->set_debug_name("first");
    first->add_style_class("alpha");
    auto second = FocusProbeWidget::create(80.0F, 40.0F);
    second->set_debug_name("second");
    second->add_style_class("beta");
    root->append(first);
    root->append(second);
    window.set_child(root);
    root->allocate({0.0F, 0.0F, 220.0F, 60.0F});

    window.dispatch_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::I,
        .modifiers = nk::Modifiers::Ctrl | nk::Modifiers::Shift,
    });
    REQUIRE(nk::has_debug_overlay_flag(window.debug_overlay_flags(),
                                       nk::DebugOverlayFlags::InspectorPanel));

    window.set_debug_selected_widget(first.get());
    window.set_debug_widget_filter("second");
    REQUIRE(window.debug_widget_filter() == "second");
    REQUIRE(window.debug_selected_widget() == second.get());

    window.set_debug_widget_filter("beta");
    REQUIRE(window.debug_selected_widget() == second.get());

    window.set_debug_widget_filter("focusprobewidget");
    REQUIRE(window.debug_selected_widget() != nullptr);

    window.set_debug_widget_filter("missing");
    REQUIRE(window.debug_selected_widget() == nullptr);

    window.set_debug_widget_filter({});
    REQUIRE(window.debug_widget_filter().empty());

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::S});
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::E});
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::C});
    REQUIRE(window.debug_widget_filter() == "sec");
    REQUIRE(window.debug_selected_widget() == second.get());

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Backspace});
    REQUIRE(window.debug_widget_filter() == "se");

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Escape});
    REQUIRE(window.debug_widget_filter().empty());
}

TEST_CASE("Widget accessibility ownership tracks default roles and widget state",
          "[app][accessibility]") {
    auto button = nk::Button::create("Launch");
    REQUIRE(button->accessible() != nullptr);
    REQUIRE(button->accessible()->role() == nk::AccessibleRole::Button);
    REQUIRE(button->accessible()->name() == "Launch");
    REQUIRE(button->accessible()->supports_action(nk::AccessibleAction::Activate));
    REQUIRE(button->accessible()->supports_action(nk::AccessibleAction::Focus));
    REQUIRE_FALSE(button->accessible()->is_hidden());
    REQUIRE(button->accessible()->state() == nk::StateFlags::None);

    bool activated = false;
    [[maybe_unused]] auto clicked_connection =
        button->on_clicked().connect([&activated]() { activated = true; });
    REQUIRE(button->accessible()->perform_action(nk::AccessibleAction::Activate));
    REQUIRE(activated);

    button->set_sensitive(false);
    REQUIRE(has_flag(button->accessible()->state(), nk::StateFlags::Disabled));

    button->set_visible(false);
    REQUIRE(button->accessible()->is_hidden());

    button->set_label("Launch Now");
    REQUIRE(button->accessible()->name() == "Launch Now");

    auto text_field = nk::TextField::create();
    text_field->set_placeholder("Search");
    REQUIRE(text_field->accessible() != nullptr);
    REQUIRE(text_field->accessible()->role() == nk::AccessibleRole::TextInput);
    REQUIRE(text_field->accessible()->description() == "Search");
    REQUIRE(text_field->accessible()->name() == "Search");
    REQUIRE(text_field->accessible()->value().empty());
    text_field->accessible()->set_relation(nk::AccessibleRelationKind::LabelledBy, "search-label");
    REQUIRE(text_field->accessible()->relations().size() == 1);
    REQUIRE(text_field->accessible()->relations()[0].target_debug_name == "search-label");

    text_field->set_text("Value");
    REQUIRE(text_field->accessible()->name() == "Search");
    REQUIRE(text_field->accessible()->value() == "Value");

    auto segmented = nk::SegmentedControl::create();
    segmented->set_segments({"Input", "Video", "Audio"});
    REQUIRE(segmented->accessible() != nullptr);
    REQUIRE(segmented->accessible()->role() == nk::AccessibleRole::TabList);
    REQUIRE(segmented->accessible()->value() == "Input");
    REQUIRE(segmented->accessible()->description().find("Audio") != std::string::npos);

    segmented->set_selected_index(1);
    REQUIRE(segmented->accessible()->value() == "Video");
}

TEST_CASE("AT-SPI snapshot flattens accessible widget trees with roles and state",
          "[app][accessibility]") {
    nk::WidgetDebugNode root;
    root.type_name = "Root";

    nk::WidgetDebugNode button;
    button.type_name = "Button";
    button.debug_name = "launch";
    button.accessible_role = "button";
    button.accessible_name = "Launch";
    button.allocation = {10.0F, 20.0F, 80.0F, 28.0F};
    button.visible = true;
    button.sensitive = true;
    button.focusable = true;
    button.focused = true;
    button.accessible_actions = {"activate", "focus"};

    nk::WidgetDebugNode hidden_label;
    hidden_label.type_name = "Label";
    hidden_label.debug_name = "hidden";
    hidden_label.accessible_role = "label";
    hidden_label.accessible_name = "Hidden";
    hidden_label.accessible_hidden = true;
    hidden_label.visible = false;

    nk::WidgetDebugNode container;
    container.type_name = "Box";
    container.debug_name = "content";
    container.children.push_back(button);
    container.children.push_back(hidden_label);

    nk::WidgetDebugNode list;
    list.type_name = "ListView";
    list.debug_name = "items";
    list.accessible_role = "list";
    list.accessible_name = "Items";
    list.allocation = {0.0F, 60.0F, 160.0F, 120.0F};
    list.visible = true;
    list.sensitive = true;

    root.children.push_back(container);
    root.children.push_back(list);

    const auto snapshot = nk::build_atspi_window_snapshot("/org/a11y/atspi/accessible",
                                                          "window-main",
                                                          "NodalKit",
                                                          {0.0F, 0.0F, 640.0F, 480.0F},
                                                          root);

    REQUIRE(snapshot.nodes.size() == 3);

    const auto* window_node =
        nk::find_atspi_accessible_node(snapshot, "/org/a11y/atspi/accessible/window_main");
    REQUIRE(window_node != nullptr);
    REQUIRE(window_node->role_name == "frame");
    REQUIRE(window_node->name == "NodalKit");
    REQUIRE(window_node->child_paths.size() == 2);

    const auto* button_node =
        nk::find_atspi_accessible_node(snapshot, "/org/a11y/atspi/accessible/window_main_n1");
    REQUIRE(button_node != nullptr);
    REQUIRE(button_node->parent_path == window_node->object_path);
    REQUIRE(button_node->role_name == "push button");
    REQUIRE(button_node->name == "Launch");
    REQUIRE(button_node->bounds == nk::Rect{10.0F, 20.0F, 80.0F, 28.0F});
    REQUIRE(button_node->action_names == std::vector<std::string>{"activate", "focus"});
    REQUIRE(nk::has_atspi_state(button_node->state, nk::AtspiStateBit::Enabled));
    REQUIRE(nk::has_atspi_state(button_node->state, nk::AtspiStateBit::Focusable));
    REQUIRE(nk::has_atspi_state(button_node->state, nk::AtspiStateBit::Focused));

    const auto* list_node =
        nk::find_atspi_accessible_node(snapshot, "/org/a11y/atspi/accessible/window_main_n2");
    REQUIRE(list_node != nullptr);
    REQUIRE(list_node->role_name == "list");
    REQUIRE(list_node->name == "Items");

    REQUIRE(nk::find_atspi_accessible_node(snapshot, "/org/a11y/atspi/accessible/window_main_n3") ==
            nullptr);
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

TEST_CASE("TextField publishes primary selection ownership and pastes it on middle click",
          "[app][text]") {
    nk::Application app(0, nullptr);

    auto source = nk::TextField::create("alpha beta");
    source->allocate({0.0F, 0.0F, 240.0F, 36.0F});
    source->select_all();
    REQUIRE(app.primary_selection_text() == "alpha beta");

    source->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Right});
    REQUIRE_FALSE(source->has_selection());
    REQUIRE(app.primary_selection_text().empty());

    source->select_all();
    REQUIRE(app.primary_selection_text() == "alpha beta");

    auto target = nk::TextField::create();
    target->allocate({0.0F, 0.0F, 240.0F, 36.0F});
    REQUIRE(target->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 0.0F, .y = 18.0F, .button = 3}));
    REQUIRE(target->text() == "alpha beta");
    REQUIRE(target->cursor_position() == target->text().size());
}

TEST_CASE("TextField keeps preedit separate until commit", "[app][text]") {
    auto field = nk::TextField::create("ab");
    field->allocate({0.0F, 0.0F, 220.0F, 36.0F});

    field->handle_text_input_event({
        .type = nk::TextInputEvent::Type::Preedit,
        .text = "xyz",
        .selection_start = 1,
        .selection_end = 3,
    });
    REQUIRE(field->text() == "ab");

    field->handle_text_input_event({
        .type = nk::TextInputEvent::Type::Commit,
        .text = "xyz",
    });
    REQUIRE(field->text() == "abxyz");

    field->handle_text_input_event({.type = nk::TextInputEvent::Type::Preedit, .text = "qq"});
    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left});
    field->handle_text_input_event({.type = nk::TextInputEvent::Type::Commit, .text = "!"});
    REQUIRE(field->text() == "abxy!z");
}

TEST_CASE("Window routes text input events to the focused widget", "[app][text]") {
    nk::Window window({.title = "Text route", .width = 240, .height = 80});
    auto field = nk::TextField::create("seed");
    window.set_child(field);
    field->allocate({0.0F, 0.0F, 220.0F, 36.0F});
    field->grab_focus();

    window.dispatch_text_input_event({
        .type = nk::TextInputEvent::Type::Commit,
        .text = "!",
    });

    REQUIRE(field->text() == "seed!");
}

TEST_CASE("Window exposes the focused text caret rect", "[app][text]") {
    nk::Window window({.title = "Caret rect", .width = 240, .height = 80});
    auto field = nk::TextField::create("seed");
    window.set_child(field);
    field->allocate({10.0F, 12.0F, 220.0F, 36.0F});
    field->grab_focus();

    const auto end_rect = window.current_text_input_caret_rect();
    REQUIRE(end_rect.has_value());
    REQUIRE(end_rect->x > 10.0F);

    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left});
    const auto moved_rect = window.current_text_input_caret_rect();
    REQUIRE(moved_rect.has_value());
    REQUIRE(moved_rect->x < end_rect->x);

    field->handle_text_input_event({
        .type = nk::TextInputEvent::Type::Preedit,
        .text = "xyz",
        .selection_start = 3,
        .selection_end = 3,
    });
    const auto preedit_rect = window.current_text_input_caret_rect();
    REQUIRE(preedit_rect.has_value());
    REQUIRE(preedit_rect->x > moved_rect->x);
}

TEST_CASE("Window exposes the focused text input state", "[app][text]") {
    nk::Window window({.title = "Text state", .width = 240, .height = 80});
    auto field = nk::TextField::create("hello");
    window.set_child(field);
    field->allocate({10.0F, 12.0F, 220.0F, 36.0F});
    field->grab_focus();
    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left});
    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Left,
        .modifiers = nk::Modifiers::Shift,
    });

    const auto state = window.current_text_input_state();
    REQUIRE(state.has_value());
    REQUIRE(state->text == "hello");
    REQUIRE(state->cursor == field->cursor_position());
    REQUIRE(state->anchor != state->cursor);
    REQUIRE(state->caret_rect.width > 0.0F);
}

TEST_CASE("TextField moves and deletes by grapheme cluster", "[app][text]") {
    auto field = nk::TextField::create(std::string("e\xCC\x81") + "x");
    field->allocate({0.0F, 0.0F, 220.0F, 36.0F});

    REQUIRE(field->cursor_position() == field->text().size());

    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left});
    REQUIRE(field->cursor_position() == std::string("e\xCC\x81").size());

    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Backspace});
    REQUIRE(field->text() == "x");
    REQUIRE(field->cursor_position() == 0);
}

TEST_CASE("TextField supports delete-surrounding text input events", "[app][text]") {
    auto field = nk::TextField::create("hello world");
    field->allocate({0.0F, 0.0F, 220.0F, 36.0F});

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Left,
        .modifiers = nk::Modifiers::Alt,
    });
    REQUIRE(field->cursor_position() == 6);

    field->handle_text_input_event({
        .type = nk::TextInputEvent::Type::DeleteSurrounding,
        .text = "",
        .delete_before_length = 6,
        .delete_after_length = 0,
    });
    REQUIRE(field->text() == "world");
    REQUIRE(field->cursor_position() == 0);
}

TEST_CASE("TextField coalesces continuous typing into a single undo step", "[app][text]") {
    auto field = nk::TextField::create("");
    field->allocate({0.0F, 0.0F, 220.0F, 36.0F});

    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::A});
    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::B});
    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::C});
    REQUIRE(field->text() == "abc");

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
    REQUIRE(field->text() == "abc");
}

TEST_CASE("TextField coalesces repeated single-character deletes", "[app][text]") {
    auto field = nk::TextField::create("abcd");
    field->allocate({0.0F, 0.0F, 220.0F, 36.0F});

    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Backspace});
    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Backspace});
    REQUIRE(field->text() == "ab");

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Z,
        .modifiers = nk::Modifiers::Ctrl,
    });
    REQUIRE(field->text() == "abcd");

    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Home});
    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Delete});
    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Delete});
    REQUIRE(field->text() == "cd");

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Z,
        .modifiers = nk::Modifiers::Ctrl,
    });
    REQUIRE(field->text() == "abcd");
}

TEST_CASE("TextField treats emoji clusters as single editing units", "[app][text]") {
    const std::string flag = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8";
    const std::string family = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F"
                               "\x91\xA7\xE2\x80\x8D\xF0\x9F\x91\xA6";
    auto field = nk::TextField::create(flag + family + "!");
    field->allocate({0.0F, 0.0F, 260.0F, 36.0F});

    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left});
    REQUIRE(field->cursor_position() == flag.size() + family.size());

    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left});
    REQUIRE(field->cursor_position() == flag.size());

    field->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Delete});
    REQUIRE(field->text() == flag + "!");
}

TEST_CASE("TextField supports word navigation and selection by modifier", "[app][text]") {
    auto field = nk::TextField::create("alpha beta gamma");
    field->allocate({0.0F, 0.0F, 260.0F, 36.0F});

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Left,
        .modifiers = nk::Modifiers::Alt,
    });
    REQUIRE(field->cursor_position() == 11);

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Left,
        .modifiers = nk::Modifiers::Alt | nk::Modifiers::Shift,
    });
    REQUIRE(field->has_selection());
    REQUIRE(field->selection_start() == 6);
    REQUIRE(field->selection_end() == 11);

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Right,
        .modifiers = nk::Modifiers::Alt,
    });
    REQUIRE_FALSE(field->has_selection());
    REQUIRE(field->cursor_position() == 11);

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Right,
        .modifiers = nk::Modifiers::Alt,
    });
    REQUIRE(field->cursor_position() == field->text().size());
}

TEST_CASE("TextField deletes words by modifier", "[app][text]") {
    auto field = nk::TextField::create("alpha beta gamma");
    field->allocate({0.0F, 0.0F, 260.0F, 36.0F});

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Backspace,
        .modifiers = nk::Modifiers::Alt,
    });
    REQUIRE(field->text() == "alpha beta ");

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Left,
        .modifiers = nk::Modifiers::Alt,
    });
    REQUIRE(field->cursor_position() == 6);

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Delete,
        .modifiers = nk::Modifiers::Alt,
    });
    REQUIRE(field->text() == "alpha  ");
}

TEST_CASE("TextField selects whole words on double click and word drag", "[app][text]") {
    auto field = nk::TextField::create("alpha beta gamma");
    field->allocate({0.0F, 0.0F, 320.0F, 36.0F});

    REQUIRE(field->handle_mouse_event({.type = nk::MouseEvent::Type::Press,
                                       .x = 62.0F,
                                       .y = 18.0F,
                                       .button = 1,
                                       .click_count = 2}));
    REQUIRE(field->has_selection());
    REQUIRE(field->selection_start() == 6);
    REQUIRE(field->selection_end() == 10);

    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Move, .x = 132.0F, .y = 18.0F, .button = 1}));
    REQUIRE(field->selection_start() == 6);
    REQUIRE(field->selection_end() == 16);

    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 132.0F, .y = 18.0F, .button = 1}));
    REQUIRE(field->selection_start() == 6);
    REQUIRE(field->selection_end() == 16);
}

TEST_CASE("TextField keeps full-word selection when double-click drag extends left",
          "[app][text]") {
    auto field = nk::TextField::create("alpha beta gamma");
    field->allocate({0.0F, 0.0F, 320.0F, 36.0F});

    REQUIRE(field->handle_mouse_event({.type = nk::MouseEvent::Type::Press,
                                       .x = 62.0F,
                                       .y = 18.0F,
                                       .button = 1,
                                       .click_count = 2}));
    REQUIRE(field->selection_start() == 6);
    REQUIRE(field->selection_end() == 10);

    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Move, .x = 20.0F, .y = 18.0F, .button = 1}));
    REQUIRE(field->selection_start() == 0);
    REQUIRE(field->selection_end() == 10);

    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 20.0F, .y = 18.0F, .button = 1}));
    REQUIRE(field->selection_start() == 0);
    REQUIRE(field->selection_end() == 10);
}

TEST_CASE("TextField extends selection anchor on shift-click", "[app][text]") {
    auto field = nk::TextField::create("alpha beta gamma");
    field->allocate({0.0F, 0.0F, 320.0F, 36.0F});

    field->handle_key_event({
        .type = nk::KeyEvent::Type::Press,
        .key = nk::KeyCode::Left,
        .modifiers = nk::Modifiers::Alt,
    });
    REQUIRE(field->cursor_position() == 11);

    REQUIRE(field->handle_mouse_event({.type = nk::MouseEvent::Type::Press,
                                       .x = 0.0F,
                                       .y = 18.0F,
                                       .button = 1,
                                       .modifiers = nk::Modifiers::Shift}));
    REQUIRE(field->has_selection());
    REQUIRE(field->selection_start() == 0);
    REQUIRE(field->selection_end() == 11);

    REQUIRE(field->handle_mouse_event({.type = nk::MouseEvent::Type::Release,
                                       .x = 0.0F,
                                       .y = 18.0F,
                                       .button = 1,
                                       .modifiers = nk::Modifiers::Shift}));
    REQUIRE(field->selection_start() == 0);
    REQUIRE(field->selection_end() == 11);
}

TEST_CASE("TextField selects the full line on triple click", "[app][text]") {
    auto field = nk::TextField::create("alpha beta gamma");
    field->allocate({0.0F, 0.0F, 320.0F, 36.0F});

    REQUIRE(field->handle_mouse_event({.type = nk::MouseEvent::Type::Press,
                                       .x = 62.0F,
                                       .y = 18.0F,
                                       .button = 1,
                                       .click_count = 3}));
    REQUIRE(field->has_selection());
    REQUIRE(field->selection_start() == 0);
    REQUIRE(field->selection_end() == field->text().size());
}

TEST_CASE("TextField keeps the full line selected when triple-click drag continues",
          "[app][text]") {
    auto field = nk::TextField::create("alpha beta gamma");
    field->allocate({0.0F, 0.0F, 320.0F, 36.0F});

    REQUIRE(field->handle_mouse_event({.type = nk::MouseEvent::Type::Press,
                                       .x = 62.0F,
                                       .y = 18.0F,
                                       .button = 1,
                                       .click_count = 3}));
    REQUIRE(field->selection_start() == 0);
    REQUIRE(field->selection_end() == field->text().size());

    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Move, .x = 140.0F, .y = 18.0F, .button = 1}));
    REQUIRE(field->selection_start() == 0);
    REQUIRE(field->selection_end() == field->text().size());

    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 8.0F, .y = 18.0F, .button = 1}));
    REQUIRE(field->selection_start() == 0);
    REQUIRE(field->selection_end() == field->text().size());
}

TEST_CASE("TextField cancels active word-drag selection when committed text replaces it",
          "[app][text]") {
    auto field = nk::TextField::create("alpha beta gamma");
    field->allocate({0.0F, 0.0F, 320.0F, 36.0F});

    REQUIRE(field->handle_mouse_event({.type = nk::MouseEvent::Type::Press,
                                       .x = 62.0F,
                                       .y = 18.0F,
                                       .button = 1,
                                       .click_count = 2}));
    REQUIRE(field->selection_start() == 6);
    REQUIRE(field->selection_end() == 10);

    REQUIRE(
        field->handle_text_input_event({.type = nk::TextInputEvent::Type::Commit, .text = "z"}));
    REQUIRE(field->text() == "alpha z gamma");
    REQUIRE_FALSE(field->has_selection());
    REQUIRE(field->cursor_position() == 7);

    REQUIRE_FALSE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Move, .x = 8.0F, .y = 18.0F, .button = 1}));
    REQUIRE_FALSE(field->has_selection());
    REQUIRE(field->cursor_position() == 7);
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

TEST_CASE("FilterListModel filters rows and preserves source mapping", "[app][model]") {
    auto source = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{"Alpha", "Beta", "Gamma", "Delta"});
    nk::FilterListModel filtered{source, [](const nk::AbstractListModel& model, std::size_t row) {
                                     return model.display_text(row).starts_with('D') ||
                                            model.display_text(row).starts_with('G');
                                 }};

    REQUIRE(filtered.source_model() == source.get());
    REQUIRE(filtered.has_filter());
    REQUIRE(filtered.row_count() == 2);
    REQUIRE(filtered.display_text(0) == "Gamma");
    REQUIRE(filtered.display_text(1) == "Delta");
    REQUIRE(filtered.map_to_source(0) == 2);
    REQUIRE(filtered.map_to_source(1) == 3);
    REQUIRE(filtered.map_from_source(0) == std::nullopt);
    REQUIRE(filtered.map_from_source(3) == 1);
}

TEST_CASE("FilterListModel rebuilds when the source changes", "[app][model]") {
    auto source = std::make_shared<nk::StringListModel>(std::vector<std::string>{"Alpha", "Beta"});
    nk::FilterListModel filtered{source, [](const nk::AbstractListModel& model, std::size_t row) {
                                     return model.display_text(row).contains('a') ||
                                            model.display_text(row).contains('A');
                                 }};

    int resets = 0;
    auto reset_conn = filtered.on_model_reset().connect([&] { ++resets; });
    (void)reset_conn;

    REQUIRE(filtered.row_count() == 2);
    source->append("Echo");
    REQUIRE(resets == 1);
    REQUIRE(filtered.row_count() == 2);

    source->append("Gamma");
    REQUIRE(resets == 2);
    REQUIRE(filtered.row_count() == 3);
    REQUIRE(filtered.display_text(2) == "Gamma");
}

TEST_CASE("SortListModel sorts rows and preserves source mapping", "[app][model]") {
    auto source =
        std::make_shared<nk::StringListModel>(std::vector<std::string>{"Gamma", "Alpha", "Beta"});
    nk::SortListModel sorted{
        source, [](const nk::AbstractListModel& model, std::size_t lhs, std::size_t rhs) {
            return model.display_text(lhs) < model.display_text(rhs);
        }};

    REQUIRE(sorted.source_model() == source.get());
    REQUIRE(sorted.has_less());
    REQUIRE(sorted.row_count() == 3);
    REQUIRE(sorted.display_text(0) == "Alpha");
    REQUIRE(sorted.display_text(1) == "Beta");
    REQUIRE(sorted.display_text(2) == "Gamma");
    REQUIRE(sorted.map_to_source(0) == 1);
    REQUIRE(sorted.map_to_source(1) == 2);
    REQUIRE(sorted.map_to_source(2) == 0);
    REQUIRE(sorted.map_from_source(0) == 2);
    REQUIRE(sorted.map_from_source(1) == 0);
}

TEST_CASE("SortListModel rebuilds when the source changes", "[app][model]") {
    auto source = std::make_shared<nk::StringListModel>(std::vector<std::string>{"Gamma", "Alpha"});
    nk::SortListModel sorted{
        source, [](const nk::AbstractListModel& model, std::size_t lhs, std::size_t rhs) {
            return model.display_text(lhs) < model.display_text(rhs);
        }};

    int resets = 0;
    auto reset_conn = sorted.on_model_reset().connect([&] { ++resets; });
    (void)reset_conn;

    source->append("Beta");
    REQUIRE(resets == 1);
    REQUIRE(sorted.row_count() == 3);
    REQUIRE(sorted.display_text(0) == "Alpha");
    REQUIRE(sorted.display_text(1) == "Beta");
    REQUIRE(sorted.display_text(2) == "Gamma");
}

TEST_CASE("DataTable sorts by header click and selects source rows", "[app][table]") {
    auto source =
        std::make_shared<nk::StringListModel>(std::vector<std::string>{"Gamma", "Alpha", "Beta"});
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    auto table = nk::DataTable::create();
    table->set_model(source);
    table->set_selection_model(selection);
    table->set_columns({nk::DataTableColumn{
        .id = "name",
        .title = "Name",
        .width = 220.0F,
        .sortable = true,
        .text = {},
    }});
    table->allocate({0.0F, 0.0F, 260.0F, 130.0F});

    REQUIRE(table->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 12.0F, .y = 12.0F, .button = 1}));
    REQUIRE(table->sort_column() == 0);
    REQUIRE(table->sort_direction() == nk::DataTableSortDirection::Ascending);

    REQUIRE(table->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 12.0F, .y = 38.0F, .button = 1}));
    REQUIRE(selection->current_row() == 1);
    REQUIRE(selection->is_selected(1));

    REQUIRE(table->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 12.0F, .y = 12.0F, .button = 1}));
    REQUIRE(table->sort_direction() == nk::DataTableSortDirection::Descending);

    REQUIRE(table->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 12.0F, .y = 38.0F, .button = 1}));
    REQUIRE(selection->current_row() == 0);
    REQUIRE(selection->is_selected(0));
}

TEST_CASE("DataTable keyboard navigation and activation use source rows", "[app][table]") {
    auto source =
        std::make_shared<nk::StringListModel>(std::vector<std::string>{"Gamma", "Alpha", "Beta"});
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    auto table = nk::DataTable::create();
    table->set_model(source);
    table->set_selection_model(selection);
    table->set_columns({nk::DataTableColumn{
        .id = "name",
        .title = "Name",
        .width = 220.0F,
        .sortable = true,
        .text = {},
    }});
    table->sort_by_column(0, nk::DataTableSortDirection::Ascending);
    table->allocate({0.0F, 0.0F, 260.0F, 130.0F});

    std::size_t activated = static_cast<std::size_t>(-1);
    auto activation_conn =
        table->on_row_activated().connect([&](std::size_t row) { activated = row; });
    (void)activation_conn;

    REQUIRE(table->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Home}));
    REQUIRE(selection->current_row() == 1);
    REQUIRE(table->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Down}));
    REQUIRE(selection->current_row() == 2);
    REQUIRE(table->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::End}));
    REQUIRE(selection->current_row() == 0);

    REQUIRE(
        table->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return}));
    REQUIRE(activated == 0);
}

TEST_CASE("DataTable resizes columns with a minimum width", "[app][table]") {
    auto source =
        std::make_shared<nk::StringListModel>(std::vector<std::string>{"Gamma", "Alpha", "Beta"});
    auto table = nk::DataTable::create();
    table->set_model(source);
    table->set_min_column_width(80.0F);
    table->set_columns({nk::DataTableColumn{
                            .id = "name",
                            .title = "Name",
                            .width = 120.0F,
                            .sortable = true,
                            .text = {},
                        },
                        nk::DataTableColumn{
                            .id = "status",
                            .title = "Status",
                            .width = 120.0F,
                            .sortable = true,
                            .text = {},
                        }});
    table->allocate({0.0F, 0.0F, 260.0F, 130.0F});

    REQUIRE(table->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 121.0F, .y = 12.0F, .button = 1}));
    REQUIRE(table->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Move, .x = 50.0F, .y = 12.0F, .button = 1}));
    REQUIRE(table->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 50.0F, .y = 12.0F, .button = 1}));

    REQUIRE(table->columns()[0].width == Catch::Approx(80.0F));
}

TEST_CASE("DataTable horizontal scroll keeps offscreen columns reachable", "[app][table]") {
    auto source =
        std::make_shared<nk::StringListModel>(std::vector<std::string>{"Gamma", "Alpha", "Beta"});
    auto table = nk::DataTable::create();
    table->set_model(source);
    table->set_columns({nk::DataTableColumn{
                            .id = "name",
                            .title = "Name",
                            .width = 180.0F,
                            .sortable = true,
                            .text = {},
                        },
                        nk::DataTableColumn{
                            .id = "status",
                            .title = "Status",
                            .width = 180.0F,
                            .sortable = true,
                            .text = [](const nk::AbstractListModel&,
                                       std::size_t row) { return row == 0 ? "Ready" : "Queued"; },
                        }});
    table->allocate({0.0F, 0.0F, 220.0F, 130.0F});

    REQUIRE(table->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Scroll, .x = 80.0F, .y = 70.0F, .scroll_dx = -4.0F}));
    REQUIRE(table->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 70.0F, .y = 12.0F, .button = 1}));
    REQUIRE(table->sort_column() == 1);
    REQUIRE(table->sort_direction() == nk::DataTableSortDirection::Ascending);
}

TEST_CASE("DataTable exposes available accessibility details", "[app][table]") {
    auto source =
        std::make_shared<nk::StringListModel>(std::vector<std::string>{"Gamma", "Alpha", "Beta"});
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    auto table = nk::DataTable::create();
    table->set_model(source);
    table->set_selection_model(selection);
    table->set_columns({nk::DataTableColumn{
        .id = "name",
        .title = "Name",
        .width = 160.0F,
        .sortable = true,
        .text = {},
    }});

    REQUIRE(table->accessible() != nullptr);
    REQUIRE(table->accessible()->role() == nk::AccessibleRole::Grid);
    REQUIRE(table->accessible()->description().find("3 rows") != std::string_view::npos);
    REQUIRE(table->accessible()->description().find("1 columns") != std::string_view::npos);

    selection->set_current_row(2);
    selection->select(2);
    REQUIRE(table->accessible()->description().find("current row 3") != std::string_view::npos);
    REQUIRE(table->accessible()->value().find("Name: Beta") != std::string_view::npos);
}

TEST_CASE("TreeModel tracks expansion and visible row mapping", "[app][tree]") {
    nk::TreeModel model;
    const auto project = model.add_root("Project");
    const auto src = model.append_child(project, "src");
    const auto main_cpp = model.append_child(src, "main.cpp");
    const auto readme = model.add_root("README.md");

    REQUIRE(model.visible_row_count() == 4);
    REQUIRE(model.visible_node_at(0) == project);
    REQUIRE(model.visible_node_at(1) == src);
    REQUIRE(model.visible_node_at(2) == main_cpp);
    REQUIRE(model.visible_node_at(3) == readme);
    REQUIRE(model.depth(main_cpp) == 2);
    REQUIRE(model.parent(main_cpp) == src);
    REQUIRE(model.visible_row_for_node(readme) == 3);

    model.set_expanded(src, false);
    REQUIRE(model.visible_row_count() == 3);
    REQUIRE(model.visible_row_for_node(main_cpp) == std::nullopt);

    model.set_expanded(project, false);
    REQUIRE(model.visible_row_count() == 2);
    REQUIRE(model.visible_node_at(0) == project);
    REQUIRE(model.visible_node_at(1) == readme);
}

TEST_CASE("TreeModel emits resets for structural, label, and expansion changes", "[app][tree]") {
    nk::TreeModel model;
    int resets = 0;
    auto reset_conn = model.on_model_reset().connect([&] { ++resets; });
    (void)reset_conn;

    const auto project = model.add_root("Project");
    REQUIRE(resets == 1);
    const auto src = model.append_child(project, "src");
    REQUIRE(resets == 2);

    model.set_display_text(src, "source");
    REQUIRE(resets == 3);
    REQUIRE(model.display_text(src) == "source");

    model.set_expanded(project, false);
    REQUIRE(resets == 4);
    model.set_expanded(project, false);
    REQUIRE(resets == 4);

    model.clear();
    REQUIRE(resets == 5);
    REQUIRE(model.visible_row_count() == 0);
    REQUIRE_FALSE(model.contains(project));

    model.clear();
    REQUIRE(resets == 5);
}

TEST_CASE("TreeView mouse toggles disclosure and selects nodes", "[app][tree]") {
    auto model = std::make_shared<nk::TreeModel>();
    const auto project = model->add_root("Project");
    const auto src = model->append_child(project, "src");
    const auto main_cpp = model->append_child(src, "main.cpp");
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    auto tree = nk::TreeView::create();
    tree->set_model(model);
    tree->set_selection_model(selection);
    tree->allocate({0.0F, 0.0F, 240.0F, 140.0F});

    REQUIRE(tree->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 12.0F, .y = 12.0F, .button = 1}));
    REQUIRE_FALSE(model->is_expanded(project));
    REQUIRE(model->visible_row_count() == 1);

    REQUIRE(tree->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 12.0F, .y = 12.0F, .button = 1}));
    REQUIRE(model->is_expanded(project));

    REQUIRE(tree->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 44.0F, .y = 38.0F, .button = 1}));
    REQUIRE(selection->current_row() == src);
    REQUIRE(selection->is_selected(src));

    REQUIRE(tree->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 30.0F, .y = 38.0F, .button = 1}));
    REQUIRE_FALSE(model->is_expanded(src));
    REQUIRE(model->visible_row_for_node(main_cpp) == std::nullopt);
}

TEST_CASE("TreeView keyboard navigation expands, collapses, and activates", "[app][tree]") {
    auto model = std::make_shared<nk::TreeModel>();
    const auto project = model->add_root("Project");
    const auto src = model->append_child(project, "src");
    const auto main_cpp = model->append_child(src, "main.cpp");
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    auto tree = nk::TreeView::create();
    tree->set_model(model);
    tree->set_selection_model(selection);
    tree->allocate({0.0F, 0.0F, 240.0F, 140.0F});

    nk::TreeNodeId activated = nk::InvalidTreeNodeId;
    auto activation_conn =
        tree->on_node_activated().connect([&](nk::TreeNodeId node) { activated = node; });
    (void)activation_conn;

    REQUIRE(tree->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Home}));
    REQUIRE(selection->current_row() == project);

    REQUIRE(tree->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Down}));
    REQUIRE(selection->current_row() == src);

    REQUIRE(tree->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Right}));
    REQUIRE(selection->current_row() == main_cpp);

    REQUIRE(tree->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left}));
    REQUIRE(selection->current_row() == src);

    REQUIRE(tree->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left}));
    REQUIRE_FALSE(model->is_expanded(src));

    REQUIRE(
        tree->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return}));
    REQUIRE(activated == src);
}

TEST_CASE("TreeView exposes accessibility details and actions", "[app][tree]") {
    auto model = std::make_shared<nk::TreeModel>();
    const auto project = model->add_root("Project");
    const auto src = model->append_child(project, "src");
    (void)src;
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    selection->set_current_row(project);
    selection->select(project);

    auto tree = nk::TreeView::create();
    tree->set_model(model);
    tree->set_selection_model(selection);

    REQUIRE(tree->accessible() != nullptr);
    REQUIRE(tree->accessible()->role() == nk::AccessibleRole::Tree);
    REQUIRE(tree->accessible()->supports_action(nk::AccessibleAction::Focus));
    REQUIRE(tree->accessible()->supports_action(nk::AccessibleAction::Activate));
    REQUIRE(tree->accessible()->supports_action(nk::AccessibleAction::Toggle));
    REQUIRE(tree->accessible()->description().find("2 visible rows") != std::string_view::npos);
    REQUIRE(tree->accessible()->description().find("current row 1 of 2") != std::string_view::npos);
    REQUIRE(tree->accessible()->description().find("expanded") != std::string_view::npos);
    REQUIRE(tree->accessible()->description().find("selected") != std::string_view::npos);
    REQUIRE(tree->accessible()->value().find("Project, row 1 of 2, expanded") !=
            std::string_view::npos);

    nk::TreeNodeId activated = nk::InvalidTreeNodeId;
    auto activation_conn =
        tree->on_node_activated().connect([&](nk::TreeNodeId node) { activated = node; });
    (void)activation_conn;
    REQUIRE(tree->accessible()->perform_action(nk::AccessibleAction::Activate));
    REQUIRE(activated == project);

    REQUIRE(tree->accessible()->perform_action(nk::AccessibleAction::Toggle));
    REQUIRE_FALSE(model->is_expanded(project));
    REQUIRE(tree->accessible()->description().find("1 visible rows") != std::string_view::npos);
    REQUIRE(tree->accessible()->value().find("collapsed") != std::string_view::npos);

    model->clear();
    REQUIRE(tree->accessible()->description().find("0 visible rows, empty") !=
            std::string_view::npos);
    REQUIRE(tree->accessible()->value().empty());
}

TEST_CASE("GridView mouse selects and activates items", "[app][grid]") {
    auto model = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{"One", "Two", "Three", "Four", "Five"});
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    auto grid = nk::GridView::create();
    grid->set_model(model);
    grid->set_selection_model(selection);
    grid->set_cell_width(80.0F);
    grid->set_cell_height(48.0F);
    grid->set_gap(4.0F);
    grid->allocate({0.0F, 0.0F, 180.0F, 170.0F});

    REQUIRE(grid->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 90.0F, .y = 58.0F, .button = 1}));
    REQUIRE(selection->current_row() == 3);
    REQUIRE(selection->is_selected(3));

    std::size_t activated = static_cast<std::size_t>(-1);
    auto activation_conn =
        grid->on_item_activated().connect([&](std::size_t row) { activated = row; });
    (void)activation_conn;
    REQUIRE(grid->handle_mouse_event({.type = nk::MouseEvent::Type::Press,
                                      .x = 90.0F,
                                      .y = 58.0F,
                                      .button = 1,
                                      .click_count = 2}));
    REQUIRE(activated == 3);
}

TEST_CASE("GridView keyboard navigation follows grid geometry", "[app][grid]") {
    auto model = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{"One", "Two", "Three", "Four", "Five", "Six"});
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    auto grid = nk::GridView::create();
    grid->set_model(model);
    grid->set_selection_model(selection);
    grid->set_cell_width(80.0F);
    grid->set_cell_height(48.0F);
    grid->set_gap(4.0F);
    grid->allocate({0.0F, 0.0F, 180.0F, 170.0F});

    REQUIRE(grid->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Home}));
    REQUIRE(selection->current_row() == 0);

    REQUIRE(grid->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Right}));
    REQUIRE(selection->current_row() == 1);

    REQUIRE(grid->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Down}));
    REQUIRE(selection->current_row() == 3);

    REQUIRE(grid->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Left}));
    REQUIRE(selection->current_row() == 2);

    REQUIRE(grid->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::End}));
    REQUIRE(selection->current_row() == 5);

    std::size_t activated = static_cast<std::size_t>(-1);
    auto activation_conn =
        grid->on_item_activated().connect([&](std::size_t row) { activated = row; });
    (void)activation_conn;
    REQUIRE(
        grid->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return}));
    REQUIRE(activated == 5);
}

TEST_CASE("GridView exposes accessibility details and actions", "[app][grid]") {
    auto model = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{"One", "Two", "Three", "Four"});
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    selection->set_current_row(1);
    selection->select(1);

    auto grid = nk::GridView::create();
    grid->set_model(model);
    grid->set_selection_model(selection);
    grid->set_cell_width(80.0F);
    grid->set_cell_height(48.0F);
    grid->set_gap(4.0F);
    grid->allocate({0.0F, 0.0F, 180.0F, 120.0F});

    REQUIRE(grid->accessible() != nullptr);
    REQUIRE(grid->accessible()->role() == nk::AccessibleRole::Grid);
    REQUIRE(grid->accessible()->supports_action(nk::AccessibleAction::Focus));
    REQUIRE(grid->accessible()->supports_action(nk::AccessibleAction::Activate));
    REQUIRE(grid->accessible()->description().find("4 items") != std::string_view::npos);
    REQUIRE(grid->accessible()->description().find("2 columns") != std::string_view::npos);
    REQUIRE(grid->accessible()->description().find("current item 2 of 4") !=
            std::string_view::npos);
    REQUIRE(grid->accessible()->description().find("Two") != std::string_view::npos);
    REQUIRE(grid->accessible()->value().find("Two, item 2 of 4") != std::string_view::npos);

    std::size_t activated = static_cast<std::size_t>(-1);
    auto activation_conn =
        grid->on_item_activated().connect([&](std::size_t row) { activated = row; });
    (void)activation_conn;
    REQUIRE(grid->accessible()->perform_action(nk::AccessibleAction::Activate));
    REQUIRE(activated == 1);

    model->clear();
    REQUIRE(grid->accessible()->description().find("0 items") != std::string_view::npos);
    REQUIRE(grid->accessible()->description().find("empty") != std::string_view::npos);
    REQUIRE(grid->accessible()->value().empty());
}

TEST_CASE("GridView ignores gaps and scrollbar gutter during hit testing", "[app][grid]") {
    auto model = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{"One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight"});
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    auto grid = nk::GridView::create();
    grid->set_model(model);
    grid->set_selection_model(selection);
    grid->set_cell_width(80.0F);
    grid->set_cell_height(48.0F);
    grid->set_gap(4.0F);
    grid->allocate({0.0F, 0.0F, 210.0F, 106.0F});

    REQUIRE(grid->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 83.0F, .y = 20.0F, .button = 1}));
    REQUIRE(selection->current_row() == static_cast<std::size_t>(-1));
    REQUIRE(selection->selected_rows().empty());

    REQUIRE_FALSE(grid->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 199.0F, .y = 20.0F, .button = 1}));
    REQUIRE(selection->current_row() == static_cast<std::size_t>(-1));
}

TEST_CASE("GridView keeps keyboard-selected items visible while scrolling", "[app][grid]") {
    auto model = std::make_shared<nk::StringListModel>(std::vector<std::string>{
        "One",
        "Two",
        "Three",
        "Four",
        "Five",
        "Six",
        "Seven",
        "Eight",
        "Nine",
        "Ten",
        "Eleven",
        "Twelve",
    });
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    auto grid = nk::GridView::create();
    grid->set_model(model);
    grid->set_selection_model(selection);
    grid->set_cell_width(80.0F);
    grid->set_cell_height(48.0F);
    grid->set_gap(4.0F);
    grid->allocate({0.0F, 0.0F, 210.0F, 106.0F});

    REQUIRE(grid->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::End}));
    REQUIRE(selection->current_row() == 11);

    REQUIRE(grid->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 2.0F, .y = 7.0F, .button = 1}));
    REQUIRE(selection->current_row() == 8);
}

TEST_CASE("GridView tracks model changes in accessibility summaries", "[app][grid]") {
    class MutableListModel final : public nk::AbstractListModel {
    public:
        explicit MutableListModel(std::vector<std::string> items) : items_(std::move(items)) {}

        [[nodiscard]] std::size_t row_count() const override { return items_.size(); }

        [[nodiscard]] std::any data(std::size_t row) const override { return items_.at(row); }

        [[nodiscard]] std::string display_text(std::size_t row) const override {
            return items_.at(row);
        }

        void rename(std::size_t row, std::string text) {
            items_.at(row) = std::move(text);
            notify_data_changed(row, row);
        }

        void remove_last() {
            REQUIRE_FALSE(items_.empty());
            begin_remove_rows(items_.size() - 1, 1);
            items_.pop_back();
            end_remove_rows();
        }

    private:
        std::vector<std::string> items_;
    };

    auto model =
        std::make_shared<MutableListModel>(std::vector<std::string>{"Alpha", "Beta", "Gamma"});
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    selection->set_current_row(1);
    selection->select(1);
    auto grid = nk::GridView::create();
    grid->set_model(model);
    grid->set_selection_model(selection);
    grid->set_cell_width(80.0F);
    grid->set_cell_height(48.0F);
    grid->set_gap(4.0F);
    grid->allocate({0.0F, 0.0F, 210.0F, 106.0F});

    REQUIRE(grid->accessible()->value().find("Beta, item 2 of 3") != std::string_view::npos);
    model->rename(1, "Beta Updated");
    REQUIRE(grid->accessible()->description().find("Beta Updated") != std::string_view::npos);
    REQUIRE(grid->accessible()->value().find("Beta Updated, item 2 of 3") !=
            std::string_view::npos);

    REQUIRE(grid->handle_mouse_event({.type = nk::MouseEvent::Type::Move, .x = 2.0F, .y = 55.0F}));
    model->remove_last();
    REQUIRE(grid->accessible()->description().find("2 items") != std::string_view::npos);
    REQUIRE(grid->accessible()->value().find("Beta Updated, item 2 of 2") !=
            std::string_view::npos);

    model->remove_last();
    REQUIRE(grid->accessible()->description().find("1 items") != std::string_view::npos);
    REQUIRE(grid->accessible()->description().find("no current item") != std::string_view::npos);
    REQUIRE(grid->accessible()->value().empty());
}

TEST_CASE("CommandPalette filters, navigates, and activates enabled commands", "[app][command]") {
    auto palette = nk::CommandPalette::create();
    palette->set_commands({
        nk::CommandPaletteCommand{.id = "file.open",
                                  .title = "Open File",
                                  .subtitle = "Choose a document",
                                  .category = "File"},
        nk::CommandPaletteCommand{.id = "file.save",
                                  .title = "Save File",
                                  .subtitle = "Write current file",
                                  .category = "File",
                                  .enabled = false},
        nk::CommandPaletteCommand{.id = "view.sidebar",
                                  .title = "Toggle Sidebar",
                                  .subtitle = "",
                                  .category = "View"},
    });
    palette->allocate({0.0F, 0.0F, 420.0F, 260.0F});

    palette->set_query("file");
    REQUIRE(palette->query() == "file");
    REQUIRE(palette->current_command() == 0);

    REQUIRE(
        palette->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Down}));
    REQUIRE(palette->current_command() == 1);

    std::string activated;
    auto activated_conn = palette->on_command_activated().connect(
        [&](std::string_view command_id) { activated = std::string(command_id); });
    (void)activated_conn;
    REQUIRE_FALSE(
        palette->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return}));
    REQUIRE(activated.empty());

    REQUIRE(palette->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Up}));
    REQUIRE(palette->current_command() == 0);
    REQUIRE(
        palette->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return}));
    REQUIRE(activated == "file.open");

    palette->set_query({});
    REQUIRE(palette->handle_text_input_event(
        {.type = nk::TextInputEvent::Type::Commit, .text = "side"}));
    REQUIRE(palette->query() == "side");
    REQUIRE(palette->current_command() == 2);
}

TEST_CASE("CommandPalette pointer activation and escape behavior", "[app][command]") {
    auto palette = nk::CommandPalette::create();
    palette->set_commands({
        nk::CommandPaletteCommand{
            .id = "file.open", .title = "Open File", .subtitle = "", .category = ""},
        nk::CommandPaletteCommand{
            .id = "view.sidebar", .title = "Toggle Sidebar", .subtitle = "", .category = ""},
    });
    palette->allocate({0.0F, 0.0F, 420.0F, 260.0F});

    std::string activated;
    auto activated_conn = palette->on_command_activated().connect(
        [&](std::string_view command_id) { activated = std::string(command_id); });
    (void)activated_conn;
    REQUIRE(palette->handle_mouse_event({.type = nk::MouseEvent::Type::Press,
                                         .x = 20.0F,
                                         .y = 112.0F,
                                         .button = 1,
                                         .click_count = 2}));
    REQUIRE(palette->current_command() == 1);
    REQUIRE(activated == "view.sidebar");

    bool dismissed = false;
    auto dismiss_conn = palette->on_dismiss_requested().connect([&] { dismissed = true; });
    (void)dismiss_conn;
    palette->set_query("open");
    REQUIRE(
        palette->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Escape}));
    REQUIRE(palette->query().empty());
    REQUIRE_FALSE(dismissed);
    REQUIRE(
        palette->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Escape}));
    REQUIRE(dismissed);
}

TEST_CASE("CommandPalette exposes accessibility details and empty state", "[app][command]") {
    auto palette = nk::CommandPalette::create();
    palette->set_commands({
        nk::CommandPaletteCommand{.id = "file.open",
                                  .title = "Open File",
                                  .subtitle = "",
                                  .category = "File"},
        nk::CommandPaletteCommand{.id = "file.save",
                                  .title = "Save File",
                                  .subtitle = "",
                                  .category = "File",
                                  .enabled = false},
    });
    palette->allocate({0.0F, 0.0F, 420.0F, 260.0F});

    REQUIRE(palette->accessible() != nullptr);
    REQUIRE(palette->accessible()->role() == nk::AccessibleRole::Dialog);
    REQUIRE(palette->accessible()->supports_action(nk::AccessibleAction::Focus));
    REQUIRE(palette->accessible()->supports_action(nk::AccessibleAction::Activate));
    REQUIRE(palette->accessible()->description().find("2 results") != std::string_view::npos);
    REQUIRE(palette->accessible()->description().find("2 commands") != std::string_view::npos);
    REQUIRE(palette->accessible()->value().find("Open File") != std::string_view::npos);

    palette->set_query("save");
    REQUIRE(palette->current_command() == 1);
    REQUIRE(palette->accessible()->description().find("1 results") != std::string_view::npos);
    REQUIRE(palette->accessible()->description().find("disabled") != std::string_view::npos);
    REQUIRE(palette->accessible()->value().find("disabled") != std::string_view::npos);
    REQUIRE_FALSE(palette->accessible()->perform_action(nk::AccessibleAction::Activate));

    palette->set_query("nothing");
    REQUIRE_FALSE(palette->current_command().has_value());
    REQUIRE(palette->accessible()->description().find("0 results") != std::string_view::npos);
    REQUIRE(palette->accessible()->description().find("empty") != std::string_view::npos);
    REQUIRE(palette->accessible()->value().empty());
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

TEST_CASE("Widget margin insets child allocation inside BoxLayout", "[app][layout]") {
    auto container = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
    container->set_layout_manager(std::move(layout));

    auto child = FixedWidget::create(60.0F, 30.0F);
    child->set_margin(nk::Insets{10.0F, 5.0F, 8.0F, 5.0F}); // top, right, bottom, left
    // Expanding so the child fills the available cross-axis width minus margins.
    child->set_horizontal_size_policy(nk::SizePolicy::Expanding);

    container->append(child);
    container->allocate({0.0F, 0.0F, 100.0F, 100.0F});

    // x = 0 + 5 (left margin), y = 0 + 10 (top margin)
    // width = 100 - 5 - 5 = 90 (Expanding fills the margin-adjusted width)
    // main-axis slot = 30 + 10 + 8 = 48 (natural + margins);
    // height = 48 - 10 - 8 = 30 (natural height).
    REQUIRE(child->allocation().x == Catch::Approx(5.0F));
    REQUIRE(child->allocation().y == Catch::Approx(10.0F));
    REQUIRE(child->allocation().width == Catch::Approx(90.0F));
    REQUIRE(child->allocation().height == Catch::Approx(30.0F));
}

TEST_CASE("Widget padding defines content_rect inside the allocation", "[app][layout]") {
    auto widget = FixedWidget::create(100.0F, 60.0F);
    widget->set_padding(nk::Insets{8.0F, 12.0F, 8.0F, 12.0F});
    widget->allocate({10.0F, 20.0F, 100.0F, 60.0F});

    const auto content = widget->content_rect();
    REQUIRE(content.x == Catch::Approx(22.0F));      // 10 + 12 left padding
    REQUIRE(content.y == Catch::Approx(28.0F));      // 20 + 8 top padding
    REQUIRE(content.width == Catch::Approx(76.0F));  // 100 - 12 - 12
    REQUIRE(content.height == Catch::Approx(44.0F)); // 60 - 8 - 8
}

TEST_CASE("Widget margin increases container natural size in BoxLayout measure", "[app][layout]") {
    auto container = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
    container->set_layout_manager(std::move(layout));

    auto child = FixedWidget::create(40.0F, 20.0F);
    child->set_margin(nk::Insets::uniform(6.0F));
    container->append(child);

    const auto request = container->measure(nk::Constraints::unbounded());
    // Natural width = 40 + 6 + 6 = 52, natural height = 20 + 6 + 6 = 32
    REQUIRE(request.natural_width == Catch::Approx(52.0F));
    REQUIRE(request.natural_height == Catch::Approx(32.0F));
}

TEST_CASE("Inheritable text-color cascades from parent to child widget", "[app][style]") {
    auto theme = nk::Theme::make_light();
    // Add a rule: widgets with class "accent-container" get blue text.
    nk::StyleRule accent_rule{};
    accent_rule.selector.classes.push_back("accent-container");
    accent_rule.properties["text-color"] = nk::Color{0.0F, 0.0F, 1.0F, 1.0F};
    theme->add_rule(accent_rule);
    nk::Theme::set_active(std::move(theme));

    auto container = TestContainer::create();
    container->add_style_class("accent-container");

    auto child = FixedWidget::create(40.0F, 20.0F);
    container->append(child);

    // The child has no "text-color" rule of its own — it should inherit
    // the blue text-color from its parent's "accent-container" class.
    const auto color = child->test_theme_color("text-color", nk::Color{1, 1, 1, 1});
    REQUIRE(color.r == Catch::Approx(0.0F));
    REQUIRE(color.g == Catch::Approx(0.0F));
    REQUIRE(color.b == Catch::Approx(1.0F));
    REQUIRE(color.a == Catch::Approx(1.0F));

    // Non-inheritable property should NOT cascade — should return the fallback.
    const auto bg = child->test_theme_color("background", nk::Color{0.5F, 0.5F, 0.5F, 1.0F});
    REQUIRE(bg.r == Catch::Approx(0.5F));
    REQUIRE(bg.g == Catch::Approx(0.5F));

    // Restore default theme.
    nk::Theme::set_active(nullptr);
}

TEST_CASE("Easing functions produce expected boundary values", "[animation]") {
    REQUIRE(nk::easing::linear(0.0F) == Catch::Approx(0.0F));
    REQUIRE(nk::easing::linear(1.0F) == Catch::Approx(1.0F));
    REQUIRE(nk::easing::linear(0.5F) == Catch::Approx(0.5F));

    REQUIRE(nk::easing::ease_in(0.0F) == Catch::Approx(0.0F));
    REQUIRE(nk::easing::ease_in(1.0F) == Catch::Approx(1.0F));
    REQUIRE(nk::easing::ease_in(0.5F) < 0.5F); // slower at start

    REQUIRE(nk::easing::ease_out(0.0F) == Catch::Approx(0.0F));
    REQUIRE(nk::easing::ease_out(1.0F) == Catch::Approx(1.0F));
    REQUIRE(nk::easing::ease_out(0.5F) > 0.5F); // faster at start

    REQUIRE(nk::easing::ease_in_out(0.0F) == Catch::Approx(0.0F));
    REQUIRE(nk::easing::ease_in_out(1.0F) == Catch::Approx(1.0F));
    REQUIRE(nk::easing::ease_in_out(0.5F) == Catch::Approx(0.5F));
}

TEST_CASE("TimedAnimation progresses from 0 to 1 over its duration", "[animation]") {
    nk::TimedAnimation anim(std::chrono::milliseconds(200), nk::easing::linear);

    REQUIRE_FALSE(anim.is_running());
    REQUIRE(anim.value() == Catch::Approx(0.0F));

    anim.start();
    REQUIRE(anim.is_running());
    REQUIRE_FALSE(anim.is_finished());

    // Immediately after start, value should be near 0.
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(anim.value_at(start) >= 0.0F);
    REQUIRE(anim.value_at(start) < 0.1F);

    // Midpoint: ~0.5.
    REQUIRE(anim.value_at(start + std::chrono::milliseconds(100)) ==
            Catch::Approx(0.5F).margin(0.05));

    // Past duration: 1.0 and finished.
    REQUIRE(anim.value_at(start + std::chrono::milliseconds(300)) == Catch::Approx(1.0F));
    REQUIRE(anim.is_finished());
}

TEST_CASE("TimedAnimation with zero duration completes immediately", "[animation]") {
    nk::TimedAnimation anim(std::chrono::milliseconds(0));
    anim.start();
    REQUIRE(anim.value() == Catch::Approx(1.0F));
    REQUIRE(anim.is_finished());
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

TEST_CASE("Tab forward and Shift+Tab backward cycle the focus chain", "[app][focus]") {
    nk::Window window({.title = "Focus chain", .width = 240, .height = 160});
    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
    layout->set_spacing(4.0F);
    root->set_layout_manager(std::move(layout));

    auto a = FocusProbeWidget::create(120.0F, 24.0F);
    auto b = FocusProbeWidget::create(120.0F, 24.0F);
    auto c = FocusProbeWidget::create(120.0F, 24.0F);
    root->append(a);
    root->append(b);
    root->append(c);
    window.set_child(root);
    root->allocate({0.0F, 0.0F, 220.0F, 120.0F});

    const auto is_focused = [](const std::shared_ptr<nk::Widget>& w) {
        return nk::has_flag(w->state_flags(), nk::StateFlags::Focused);
    };

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    REQUIRE(is_focused(a));

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    REQUIRE(is_focused(b));
    REQUIRE_FALSE(is_focused(a));

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    REQUIRE(is_focused(c));

    // Wrap past the last focusable widget back to the first.
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    REQUIRE(is_focused(a));

    // Reverse direction: Shift+Tab walks backward and wraps at the start.
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press,
                               .key = nk::KeyCode::Tab,
                               .modifiers = nk::Modifiers::Shift});
    REQUIRE(is_focused(c));

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press,
                               .key = nk::KeyCode::Tab,
                               .modifiers = nk::Modifiers::Shift});
    REQUIRE(is_focused(b));
}

TEST_CASE("Focus navigation skips invisible and disabled widgets", "[app][focus]") {
    nk::Window window({.title = "Focus skip", .width = 240, .height = 160});
    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
    layout->set_spacing(4.0F);
    root->set_layout_manager(std::move(layout));

    auto a = FocusProbeWidget::create(120.0F, 24.0F);
    auto hidden = FocusProbeWidget::create(120.0F, 24.0F);
    auto disabled = FocusProbeWidget::create(120.0F, 24.0F);
    auto d = FocusProbeWidget::create(120.0F, 24.0F);
    hidden->set_visible(false);
    disabled->set_sensitive(false);

    root->append(a);
    root->append(hidden);
    root->append(disabled);
    root->append(d);
    window.set_child(root);
    root->allocate({0.0F, 0.0F, 220.0F, 120.0F});

    const auto is_focused = [](const std::shared_ptr<nk::Widget>& w) {
        return nk::has_flag(w->state_flags(), nk::StateFlags::Focused);
    };

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    REQUIRE(is_focused(a));

    // Tab must jump over both hidden and disabled siblings to reach `d`.
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    REQUIRE(is_focused(d));
    REQUIRE_FALSE(is_focused(hidden));
    REQUIRE_FALSE(is_focused(disabled));
}

TEST_CASE("Focus clears when the focused widget becomes hidden or disabled", "[app][focus]") {
    nk::Window window({.title = "Focus clear", .width = 240, .height = 160});
    auto root = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
    root->set_layout_manager(std::move(layout));

    auto probe = FocusProbeWidget::create(120.0F, 24.0F);
    root->append(probe);
    window.set_child(root);
    root->allocate({0.0F, 0.0F, 220.0F, 60.0F});

    const auto is_focused = [&]() {
        return nk::has_flag(probe->state_flags(), nk::StateFlags::Focused);
    };

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    REQUIRE(is_focused());

    probe->set_visible(false);
    REQUIRE_FALSE(is_focused());

    // Bring it back and re-focus, then flip the sensitivity bit instead.
    probe->set_visible(true);
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    REQUIRE(is_focused());

    probe->set_sensitive(false);
    REQUIRE_FALSE(is_focused());
}

TEST_CASE("BoxLayout ignores invisible children for measure and allocate", "[app][layout]") {
    auto container = TestContainer::create();
    auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
    layout->set_spacing(4.0F);
    container->set_layout_manager(std::move(layout));

    auto visible_a = FixedWidget::create(40.0F, 20.0F);
    auto invisible = FixedWidget::create(40.0F, 200.0F);
    auto visible_b = FixedWidget::create(40.0F, 20.0F);
    invisible->set_visible(false);

    container->append(visible_a);
    container->append(invisible);
    container->append(visible_b);

    // Natural size must not include the invisible child's 200 px height and
    // must drop the spacing slot that would otherwise sit next to it.
    const auto request = container->measure(nk::Constraints::unbounded());
    REQUIRE(request.natural_height == Catch::Approx(44.0F)); // 20 + 4 + 20

    container->allocate({0.0F, 0.0F, 100.0F, 44.0F});
    REQUIRE(visible_a->allocation().y == Catch::Approx(0.0F));
    REQUIRE(visible_b->allocation().y == Catch::Approx(24.0F));
    // Invisible widgets keep their previous (zero) allocation.
    REQUIRE(invisible->allocation().width == Catch::Approx(0.0F));
    REQUIRE(invisible->allocation().height == Catch::Approx(0.0F));
}

TEST_CASE("Horizontal BoxLayout distributes extra width by stretch", "[app][layout]") {
    auto container = TestContainer::create();
    container->set_layout_manager(std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal));

    auto expand_a = FixedWidget::create(20.0F, 30.0F);
    auto expand_b = FixedWidget::create(20.0F, 30.0F);
    expand_a->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    expand_a->set_horizontal_stretch(3);
    expand_b->set_horizontal_size_policy(nk::SizePolicy::Expanding);
    expand_b->set_horizontal_stretch(1);

    container->append(expand_a);
    container->append(expand_b);
    // 200 px wide layout - 40 px natural (2 * 20) = 160 px extra.
    // With stretch weights 3:1 → expand_a gets +120 (140 total), expand_b +40 (60 total).
    container->allocate({0.0F, 0.0F, 200.0F, 30.0F});

    REQUIRE(expand_a->allocation().width == Catch::Approx(140.0F));
    REQUIRE(expand_b->allocation().width == Catch::Approx(60.0F));
    REQUIRE(expand_a->allocation().x == Catch::Approx(0.0F));
    REQUIRE(expand_b->allocation().x == Catch::Approx(140.0F));
}

TEST_CASE("WindowEvent::Resize updates size, fires signal, and requests a Resize frame",
          "[app][platform]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("software") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Resize event", .width = 240, .height = 160});
    window.set_child(nk::Label::create("resize"));
    window.present();
    REQUIRE(app.event_loop().poll());

    int resize_events = 0;
    int resize_width = 0;
    int resize_height = 0;
    auto resize_conn = window.on_resize().connect([&](int w, int h) {
        ++resize_events;
        resize_width = w;
        resize_height = h;
    });
    (void)resize_conn;

    const auto baseline_frame = window.last_frame_diagnostics().frame_id;
    window.dispatch_window_event(
        {.type = nk::WindowEvent::Type::Resize, .width = 300, .height = 220});
    REQUIRE(app.event_loop().poll());

    REQUIRE(resize_events == 1);
    REQUIRE(resize_width == 300);
    REQUIRE(resize_height == 220);
    // Note: Window::size() reads from the live platform surface once present()
    // has been called, so its value reflects the compositor-reported size
    // rather than the event width/height. The resize signal payload is the
    // authoritative observable for the Resize event contract.
    REQUIRE(window.last_frame_diagnostics().frame_id > baseline_frame);
    REQUIRE(nk::has_frame_request_reason(window.last_frame_diagnostics(),
                                         nk::FrameRequestReason::Resize));
}

TEST_CASE("WindowEvent::ScaleFactorChanged fires signal and requests a ScaleFactorChanged frame",
          "[app][platform]") {
    ScopedTestEnvVar renderer_backend_override("NK_RENDERER_BACKEND");
    REQUIRE(renderer_backend_override.set("software") == 0);

    nk::Application app(0, nullptr);
    nk::Window window({.title = "Scale event", .width = 240, .height = 160});
    window.set_child(nk::Label::create("scale"));
    window.present();
    REQUIRE(app.event_loop().poll());

    float observed_scale = 0.0F;
    int scale_events = 0;
    auto scale_conn = window.on_scale_factor_changed().connect([&](float s) {
        ++scale_events;
        observed_scale = s;
    });
    (void)scale_conn;

    const auto baseline_frame = window.last_frame_diagnostics().frame_id;
    window.dispatch_window_event(
        {.type = nk::WindowEvent::Type::ScaleFactorChanged, .scale_factor = 2.0F});
    REQUIRE(app.event_loop().poll());

    REQUIRE(scale_events == 1);
    REQUIRE(observed_scale == Catch::Approx(2.0F));
    REQUIRE(window.last_frame_diagnostics().frame_id > baseline_frame);
    REQUIRE(nk::has_frame_request_reason(window.last_frame_diagnostics(),
                                         nk::FrameRequestReason::ScaleFactorChanged));
}
