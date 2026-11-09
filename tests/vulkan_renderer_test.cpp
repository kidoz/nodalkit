#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/platform/window_inspector.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/text_shaper.h>
#include <nk/widgets/canvas_widget.h>
#include <string>
#include <vector>
#if defined(__linux__)
#include <dlfcn.h>
#endif

namespace {

class VulkanEnvironment {
public:
    VulkanEnvironment() {
        if (const auto* value = std::getenv("NK_RENDERER_BACKEND")) {
            previous_ = value;
            had_previous_ = true;
        }
#if defined(_WIN32)
        _putenv_s("NK_RENDERER_BACKEND", "vulkan");
#else
        setenv("NK_RENDERER_BACKEND", "vulkan", 1);
#endif
    }

    ~VulkanEnvironment() {
#if defined(_WIN32)
        _putenv_s("NK_RENDERER_BACKEND", had_previous_ ? previous_.c_str() : "");
#else
        if (had_previous_) {
            setenv("NK_RENDERER_BACKEND", previous_.c_str(), 1);
        } else {
            unsetenv("NK_RENDERER_BACKEND");
        }
#endif
    }

private:
    std::string previous_;
    bool had_previous_ = false;
};

bool native_vulkan_available() {
    bool available = false;
#if defined(__linux__)
    available = nk::renderer_backend_available(nk::RendererBackend::Vulkan) &&
                std::getenv("WAYLAND_DISPLAY") != nullptr;
#elif defined(_WIN32)
    available = nk::renderer_backend_available(nk::RendererBackend::Vulkan);
#endif
    if (std::getenv("NK_REQUIRE_VULKAN") != nullptr) {
        REQUIRE(available);
    }
    return available;
}

void check_rgb(const nk::DebugScreenshot& shot,
               nk::Point point,
               float scale,
               std::array<int, 3> expected,
               int tolerance = 2) {
    const int x = static_cast<int>(point.x * scale);
    const int y = static_cast<int>(point.y * scale);
    REQUIRE(x >= 0);
    REQUIRE(y >= 0);
    REQUIRE(x < shot.width);
    REQUIRE(y < shot.height);
    const auto offset = (static_cast<std::size_t>(y) * static_cast<std::size_t>(shot.width) +
                         static_cast<std::size_t>(x)) *
                        4U;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        CHECK(std::abs(static_cast<int>(shot.rgba[offset + channel]) - expected[channel]) <=
              tolerance);
    }
}

} // namespace

TEST_CASE("Vulkan paints effects and strokes without software rasterization", "[vulkan][render]") {
    if (!native_vulkan_available()) {
        SKIP("Requires a native Vulkan surface");
    }
    VulkanEnvironment environment;
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan effects regression", .width = 380, .height = 240});
    auto canvas = nk::CanvasWidget::create();
    auto connection = canvas->on_draw().connect([](nk::SnapshotContext& ctx, nk::Rect a) {
        auto black = nk::Color::from_rgb(0, 0, 0);
        ctx.add_color_rect(a, nk::Color::from_rgb(255, 255, 255));
        ctx.add_linear_gradient({a.x + 20, a.y + 20, 40, 40}, black, black);
        ctx.push_opacity({a.x + 80, a.y + 20, 40, 40}, 0.5F);
        ctx.push_opacity({a.x + 80, a.y + 20, 40, 40}, 0.5F);
        ctx.add_color_rect({a.x + 80, a.y + 20, 40, 40}, black);
        ctx.pop_container();
        ctx.pop_container();
        ctx.add_shadow({a.x + 140, a.y + 20, 40, 40}, black, 0, 0, 4);
        ctx.add_line({a.x + 210, a.y + 20}, {a.x + 250, a.y + 60}, black, 4);
        ctx.add_line({a.x + 280, a.y + 40}, {a.x + 280, a.y + 40}, black, 6);
    });
    window.set_child(canvas);
    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(window.renderer_backend() == nk::RendererBackend::Vulkan);
    const auto path =
        window.inspector().last_frame_diagnostics().render_hotspot_counters.gpu_present_path;
    REQUIRE(path != nk::GpuPresentPath::SoftwareUpload);
    REQUIRE(path != nk::GpuPresentPath::SoftwareDirect);
    REQUIRE(path != nk::GpuPresentPath::None);
    const auto shot = window.inspector().capture_debug_screenshot();
    REQUIRE(shot);
    REQUIRE(shot->source_backend == nk::RendererBackend::Vulkan);
    const auto a = canvas->allocation();
    for (float x : {40.0F, 160.0F, 230.0F, 280.0F}) {
        check_rgb(*shot, {a.x + x, a.y + 40}, window.scale_factor(), {0, 0, 0});
    }
    check_rgb(*shot, {a.x + 100, a.y + 40}, window.scale_factor(), {191, 191, 191});
    CHECK(connection.connected());
}

TEST_CASE("Vulkan preserves transformed and path content through fallback", "[vulkan][render]") {
    if (!native_vulkan_available()) {
        SKIP("Requires a native Vulkan surface");
    }
    VulkanEnvironment environment;
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan affine fallback regression", .width = 240, .height = 180});
    auto canvas = nk::CanvasWidget::create();
    auto connection = canvas->on_draw().connect([](nk::SnapshotContext& ctx, nk::Rect a) {
        auto black = nk::Color::from_rgb(0, 0, 0);
        ctx.add_color_rect(a, nk::Color::from_rgb(255, 255, 255));
        ctx.push_transform({.m31 = a.x + 80, .m32 = a.y + 20});
        ctx.add_color_rect({-60, 0, 40, 40}, black);
        ctx.pop_transform();
        ctx.add_path({.points = {{a.x + 100, a.y + 20}, {a.x + 140, a.y + 60}}}, black, 4);
    });
    window.set_child(canvas);
    window.present();
    REQUIRE(app.event_loop().poll());
    const auto shot = window.inspector().capture_debug_screenshot();
    REQUIRE(shot);
    REQUIRE(shot->source_backend == nk::RendererBackend::Vulkan);
    const auto a = canvas->allocation();
    check_rgb(*shot, {a.x + 40, a.y + 40}, window.scale_factor(), {0, 0, 0});
    check_rgb(*shot, {a.x + 120, a.y + 40}, window.scale_factor(), {0, 0, 0});
    CHECK(connection.connected());
}

TEST_CASE("Vulkan draws affine content and independent clip spaces on the GPU",
          "[vulkan][render][affine]") {
    if (!native_vulkan_available()) {
        SKIP("Requires a native Vulkan surface");
    }
    VulkanEnvironment environment;
    const auto matrix = GENERATE(
        nk::Matrix3x2{},
        nk::Matrix3x2{.m11 = 1.5F, .m22 = 0.75F},
        nk::Matrix3x2{.m11 = 0, .m12 = 1, .m21 = -1, .m22 = 0, .m31 = 110},
        nk::Matrix3x2{.m11 = -1, .m31 = 110},
        nk::Matrix3x2{.m21 = 0.4F},
        nk::Matrix3x2{.m11 = 0.8660254F, .m12 = 0.5F, .m21 = -0.5F, .m22 = 0.8660254F, .m31 = 55});
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan affine regression", .width = 280, .height = 260});
    const std::array<uint32_t, 4> pixels{0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFF000000};
    auto draw = [&](nk::SnapshotContext& ctx, nk::Rect a) {
        const auto black = nk::Color::from_rgb(0, 0, 0);
        ctx.add_color_rect(a, nk::Color::from_rgb(255, 255, 255));
        // The outer clip stays in root space; the inner clip rotates/scales.
        ctx.push_rounded_clip({a.x + 25, a.y + 25, 160, 150}, 12);
        ctx.push_transform({.m31 = a.x + 40, .m32 = a.y + 40});
        ctx.push_transform(matrix);
        ctx.push_rounded_clip({0, 0, 100, 100}, 10);
        ctx.add_color_rect({-20, -20, 140, 140}, black);
        ctx.add_image({10, 10, 80, 80}, pixels.data(), 2, 2);
        ctx.push_opacity({20, 20, 20, 20}, 0.5F);
        ctx.add_rounded_rect({20, 20, 20, 20}, black, 4);
        ctx.pop_container();
        ctx.pop_container();
        ctx.pop_transform();
        ctx.pop_transform();
        ctx.pop_container();
        // Nonfinite and singular transforms have no visible output.
        ctx.push_transform({.m11 = 0, .m22 = 0});
        ctx.add_color_rect(a, black);
        ctx.pop_transform();
        ctx.push_transform({.m31 = std::numeric_limits<float>::quiet_NaN()});
        ctx.add_color_rect(a, black);
        ctx.pop_transform();
    };
    auto canvas = nk::CanvasWidget::create();
    auto connection = canvas->on_draw().connect(draw);
    window.set_child(canvas);
    window.present();
    REQUIRE(app.event_loop().poll());
    const auto path =
        window.inspector().last_frame_diagnostics().render_hotspot_counters.gpu_present_path;
    REQUIRE(path != nk::GpuPresentPath::None);
    REQUIRE(path != nk::GpuPresentPath::SoftwareUpload);
    REQUIRE(path != nk::GpuPresentPath::SoftwareDirect);
    const auto shot = window.inspector().capture_debug_screenshot();
    REQUIRE(shot);
    REQUIRE(shot->source_backend == nk::RendererBackend::Vulkan);
    const float scale = window.scale_factor();
    nk::SnapshotContext reference_context;
    draw(reference_context, canvas->allocation());
    const auto root = reference_context.take_root();
    nk::SoftwareRenderer reference;
    reference.begin_frame({shot->width / scale, shot->height / scale}, scale);
    reference.render(*root);
    reference.end_frame();
    // Software transforms resample a rasterized layer with nearest sampling;
    // GPU transforms rasterize geometry directly. Compare flat interiors and
    // exteriors, excluding a three-device-pixel neighborhood of reference edges.
    std::size_t colored_samples = 0;
    std::size_t mismatches = 0;
    const auto* expected = reference.pixel_data();
    const auto a = canvas->allocation();
    for (int y = static_cast<int>(a.y * scale) + 4; y < (a.bottom() * scale) - 4; ++y) {
        for (int x = static_cast<int>(a.x * scale) + 4; x < (a.right() * scale) - 4; ++x) {
            const auto offset = (static_cast<std::size_t>(y) * shot->width + x) * 4;
            bool flat = true;
            for (int dy = -3; dy <= 3 && flat; ++dy) {
                for (int dx = -3; dx <= 3 && flat; ++dx) {
                    const auto neighbor =
                        (static_cast<std::size_t>(y + dy) * shot->width + x + dx) * 4;
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        flat = flat && expected[offset + channel] == expected[neighbor + channel];
                    }
                }
            }
            if (!flat) {
                continue;
            }
            colored_samples += expected[offset] != 255 || expected[offset + 1] != 255 ||
                               expected[offset + 2] != 255;
            for (std::size_t channel = 0; channel < 3; ++channel) {
                mismatches += std::abs(static_cast<int>(shot->rgba[offset + channel]) -
                                       expected[offset + channel]) > 2;
            }
        }
    }
    CHECK(colored_samples > 1000);
    CHECK(mismatches == 0);
    CHECK(connection.connected());
}

TEST_CASE("Vulkan reuses affine textures and redraws transformed damage",
          "[vulkan][render][affine]") {
    if (!native_vulkan_available()) {
        SKIP("Requires a native Vulkan surface");
    }
    VulkanEnvironment environment;
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan affine cache regression", .width = 320, .height = 220});
    std::array<uint32_t, 4> pixels{0xFF000000, 0xFF000000, 0xFF000000, 0xFF000000};
    float translation = 0;
    auto canvas = nk::CanvasWidget::create();
    auto draw = [&](nk::SnapshotContext& ctx, nk::Rect a) {
        const auto black = nk::Color::from_rgb(0, 0, 0);
        ctx.add_color_rect(a, nk::Color::from_rgb(255, 255, 255));
        ctx.push_transform({.m31 = a.x + 100 + translation, .m32 = a.y + 20});
        ctx.add_image({-80, 0, 40, 40}, pixels.data(), 2, 2);
        ctx.add_text({-80, 60}, "Affine", black, {.family = "DejaVu Sans", .size = 14});
        ctx.add_border({-30, 0, 40, 40}, black, 6, 4);
        ctx.add_line({30, 0}, {70, 40}, black, 4);
        ctx.add_linear_gradient({-30, 60, 40, 40}, black, black);
        ctx.add_shadow({30, 60, 40, 40}, black, 0, 0, 4);
        ctx.pop_transform();
        ctx.add_color_rect({a.x + 240, a.y + 120, 20, 20}, black);
    };
    auto connection = canvas->on_draw().connect(draw);
    window.set_child(canvas);
    window.present();
    REQUIRE(app.event_loop().poll());
    for (int frame = 0; frame < 4; ++frame) {
        translation = static_cast<float>(frame * 10);
        canvas->queue_redraw({20, 20, 200, 120});
        REQUIRE(app.event_loop().poll());
        const auto counters = window.inspector().last_frame_diagnostics().render_hotspot_counters;
        REQUIRE(counters.gpu_present_path != nk::GpuPresentPath::SoftwareUpload);
        REQUIRE(counters.gpu_present_path != nk::GpuPresentPath::SoftwareDirect);
        REQUIRE(counters.gpu_present_path != nk::GpuPresentPath::None);
        REQUIRE(counters.gpu_present_path == nk::GpuPresentPath::PartialRedrawCopy);
        CHECK(counters.gpu_skipped_command_count > 0);
        CHECK(counters.texture_allocation_count == 0);
        CHECK(counters.image_texture_upload_count == 0);
        CHECK(counters.text_texture_upload_count == 0);
        const auto shot = window.inspector().capture_debug_screenshot();
        REQUIRE(shot);
        REQUIRE(shot->source_backend == nk::RendererBackend::Vulkan);
        const auto a = canvas->allocation();
        check_rgb(*shot, {a.x + 40 + translation, a.y + 40}, window.scale_factor(), {0, 0, 0});
        check_rgb(*shot, {a.x + 72 + translation, a.y + 40}, window.scale_factor(), {0, 0, 0});
        check_rgb(*shot, {a.x + 150 + translation, a.y + 40}, window.scale_factor(), {0, 0, 0});
        check_rgb(*shot, {a.x + 90 + translation, a.y + 100}, window.scale_factor(), {0, 0, 0});
        check_rgb(*shot, {a.x + 150 + translation, a.y + 100}, window.scale_factor(), {0, 0, 0});
        // Compare every glyph pixel for an integer-device-pixel translation.
        auto shaper = nk::TextShaper::create();
        REQUIRE(shaper);
        nk::SnapshotContext context(shaper.get());
        draw(context, a);
        const auto root = context.take_root();
        nk::SoftwareRenderer reference;
        reference.set_text_shaper(shaper.get());
        const float scale = window.scale_factor();
        reference.begin_frame({shot->width / scale, shot->height / scale}, scale);
        reference.render(*root);
        reference.end_frame();
        std::size_t glyph_mismatches = 0;
        std::size_t glyph_pixels = 0;
        for (int y = static_cast<int>((a.y + 80) * scale); y < (a.y + 110) * scale; ++y) {
            for (int x = static_cast<int>((a.x + 20 + translation) * scale);
                 x < (a.x + 66 + translation) * scale;
                 ++x) {
                const auto offset = (static_cast<std::size_t>(y) * shot->width + x) * 4;
                glyph_pixels += reference.pixel_data()[offset] < 250;
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    glyph_mismatches += std::abs(static_cast<int>(shot->rgba[offset + channel]) -
                                                 reference.pixel_data()[offset + channel]) > 2;
                }
            }
        }
        CHECK(glyph_pixels > 20);
        CHECK(glyph_mismatches == 0);
    }
    CHECK(connection.connected());
}

TEST_CASE("Vulkan preserves clip capacity across affine coordinate spaces",
          "[vulkan][render][affine]") {
    if (!native_vulkan_available()) {
        SKIP("Requires a native Vulkan surface");
    }
    VulkanEnvironment environment;
    const int depth = GENERATE(4, 5);
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan affine clip capacity", .width = 240, .height = 200});
    auto canvas = nk::CanvasWidget::create();
    auto connection = canvas->on_draw().connect([&](nk::SnapshotContext& ctx, nk::Rect a) {
        ctx.add_color_rect(a, nk::Color::from_rgb(255, 255, 255));
        ctx.push_transform({.m31 = a.x + 20, .m32 = a.y + 20});
        // Many same-space rectangular clips still flatten into one slot.
        for (int i = 0; i < 8; ++i) {
            ctx.push_rounded_clip({0, 0, 120, 100}, 0);
        }
        for (int i = 1; i < depth; ++i) {
            ctx.push_transform({.m31 = 1, .m32 = 1});
            ctx.push_rounded_clip({0, 0, 120, 100}, 10);
        }
        ctx.add_color_rect({0, 0, 120, 100}, nk::Color::from_rgb(0, 0, 0));
        for (int i = 1; i < depth; ++i) {
            ctx.pop_container();
            ctx.pop_transform();
        }
        for (int i = 0; i < 8; ++i) {
            ctx.pop_container();
        }
        ctx.pop_transform();
    });
    window.set_child(canvas);
    window.present();
    REQUIRE(app.event_loop().poll());
    const auto path =
        window.inspector().last_frame_diagnostics().render_hotspot_counters.gpu_present_path;
    if (depth == 4) {
        CHECK(path != nk::GpuPresentPath::None);
        CHECK(path != nk::GpuPresentPath::SoftwareDirect);
        CHECK(path != nk::GpuPresentPath::SoftwareUpload);
    } else {
        CHECK(path == nk::GpuPresentPath::SoftwareUpload);
    }
    const auto shot = window.inspector().capture_debug_screenshot();
    REQUIRE(shot);
    REQUIRE(shot->source_backend == nk::RendererBackend::Vulkan);
    const auto a = canvas->allocation();
    check_rgb(*shot, {a.x + 60, a.y + 60}, window.scale_factor(), {0, 0, 0});
    check_rgb(*shot, {a.x + 150, a.y + 60}, window.scale_factor(), {255, 255, 255});
    CHECK(connection.connected());
}

TEST_CASE("Vulkan grows and reuses affine uniform storage between frames",
          "[vulkan][render][affine]") {
    if (!native_vulkan_available()) {
        SKIP("Requires a native Vulkan surface");
    }
    VulkanEnvironment environment;
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan affine buffer growth", .width = 280, .height = 240});
    int count = 16;
    auto canvas = nk::CanvasWidget::create();
    auto connection = canvas->on_draw().connect([&](nk::SnapshotContext& ctx, nk::Rect a) {
        ctx.add_color_rect(a, nk::Color::from_rgb(255, 255, 255));
        for (int index = 0; index < count; ++index) {
            ctx.push_transform({.m31 = a.x + 20 + static_cast<float>(index % 40) * 5,
                                .m32 = a.y + 20 + static_cast<float>(index / 40) * 5});
            ctx.add_color_rect({-10, -10, 4, 4}, nk::Color::from_rgb(0, 0, 0));
            ctx.pop_transform();
        }
    });
    window.set_child(canvas);
    window.present();
    REQUIRE(app.event_loop().poll());
    for (int next_count : {600, 1000, 32}) {
        count = next_count;
        canvas->queue_redraw();
        REQUIRE(app.event_loop().poll());
        const auto path =
            window.inspector().last_frame_diagnostics().render_hotspot_counters.gpu_present_path;
        REQUIRE(path != nk::GpuPresentPath::None);
        REQUIRE(path != nk::GpuPresentPath::SoftwareUpload);
        REQUIRE(path != nk::GpuPresentPath::SoftwareDirect);
        const auto shot = window.inspector().capture_debug_screenshot();
        REQUIRE(shot);
        REQUIRE(shot->source_backend == nk::RendererBackend::Vulkan);
        const auto a = canvas->allocation();
        check_rgb(*shot, {a.x + 12, a.y + 12}, window.scale_factor(), {0, 0, 0});
        check_rgb(*shot,
                  {a.x + 12 + static_cast<float>((count - 1) % 40) * 5,
                   a.y + 12 + static_cast<float>((count - 1) / 40) * 5},
                  window.scale_factor(),
                  {0, 0, 0});
        check_rgb(*shot, {a.x + 250, a.y + 180}, window.scale_factor(), {255, 255, 255});
    }
    CHECK(connection.connected());
}

TEST_CASE("Vulkan batches uploads and reuses streaming image allocations", "[vulkan][render]") {
    if (!native_vulkan_available()) {
        SKIP("Requires a native Vulkan surface");
    }
    VulkanEnvironment environment;
    const bool explicit_revision = GENERATE(false, true);
    uint64_t revision = 0;
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan streaming regression", .width = 320, .height = 200});
    std::array<std::vector<uint32_t>, 4> images;
    for (auto& image : images) {
        image.resize(16U * 16U, 0xFF000000);
    }
    auto canvas = nk::CanvasWidget::create();
    auto connection = canvas->on_draw().connect([&](nk::SnapshotContext& ctx, nk::Rect a) {
        ctx.add_color_rect(a, nk::Color::from_rgb(255, 255, 255));
        for (std::size_t i = 0; i < images.size(); ++i) {
            const nk::Rect rect{a.x + 20 + static_cast<float>(i) * 50, a.y + 20, 40, 40};
            if (explicit_revision) {
                ctx.add_image(
                    rect, images[i].data(), 16, 16, nk::ScaleMode::NearestNeighbor, revision);
            } else {
                ctx.add_image(rect, images[i].data(), 16, 16);
            }
        }
    });
    window.set_child(canvas);
    window.present();
    REQUIRE(app.event_loop().poll());
    auto counters = window.inspector().last_frame_diagnostics().render_hotspot_counters;
    REQUIRE(counters.image_texture_upload_count == 4);
    REQUIRE(counters.texture_upload_batch_count == 1);
    REQUIRE(counters.texture_upload_byte_count >= 4U * 16U * 16U * 4U);
    const auto resident_bytes = counters.texture_cache_byte_count;
    REQUIRE(resident_bytes > 0);
    for (int frame = 0; frame < 24; ++frame) {
        ++revision;
        const uint32_t color = (frame % 2 == 0) ? 0xFFFF0000 : 0xFF0000FF;
        for (auto& image : images) {
            std::fill(image.begin(), image.end(), color);
        }
        canvas->queue_redraw();
        REQUIRE(app.event_loop().poll());
        counters = window.inspector().last_frame_diagnostics().render_hotspot_counters;
        REQUIRE(counters.gpu_present_path != nk::GpuPresentPath::SoftwareUpload);
        CHECK(counters.texture_allocation_count == 0);
        CHECK(counters.image_texture_upload_count == 4);
        CHECK(counters.texture_upload_batch_count == 1);
        CHECK(counters.texture_cache_byte_count == resident_bytes);
        if (frame == 0 || frame == 23) {
            const auto shot = window.inspector().capture_debug_screenshot();
            REQUIRE(shot);
            REQUIRE(shot->source_backend == nk::RendererBackend::Vulkan);
            const auto a = canvas->allocation();
            check_rgb(*shot,
                      {a.x + 40, a.y + 40},
                      window.scale_factor(),
                      frame % 2 == 0 ? std::array{255, 0, 0} : std::array{0, 0, 255});
        }
    }
    window.request_frame();
    REQUIRE(app.event_loop().poll());
    counters = window.inspector().last_frame_diagnostics().render_hotspot_counters;
    CHECK(counters.image_texture_upload_count == 0);
    CHECK(counters.texture_upload_batch_count == 0);
    CHECK(counters.texture_upload_byte_count == 0);
    CHECK(connection.connected());
}

TEST_CASE("Vulkan recovers after a failed frame", "[vulkan][render][recovery]") {
#if defined(__linux__)
    const auto* fault = std::getenv("NK_TEST_VULKAN_FAULT");
    if (fault == nullptr || !native_vulkan_available()) {
        SKIP("Requires the Linux Vulkan fault interposer and a native surface");
    }
    const auto injected =
        reinterpret_cast<bool (*)()>(dlsym(RTLD_DEFAULT, "nk_vulkan_test_fault_injected"));
    REQUIRE(injected != nullptr);
    VulkanEnvironment environment;
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan frame recovery", .width = 240, .height = 180});
    std::array<uint32_t, 16> pixels{};
    pixels.fill(0xFFFF0000);
    auto canvas = nk::CanvasWidget::create();
    auto connection = canvas->on_draw().connect([&](nk::SnapshotContext& ctx, nk::Rect a) {
        ctx.add_color_rect(a, nk::Color::from_rgb(255, 255, 255));
        ctx.add_color_rect({a.x + 20, a.y + 20, 40, 40}, nk::Color::from_rgb(0, 0, 0));
        ctx.add_image({a.x + 80, a.y + 20, 40, 40}, pixels.data(), 4, 4);
    });
    window.set_child(canvas);
    window.present();
    REQUIRE(app.event_loop().poll());
    REQUIRE(injected());
    pixels.fill(0xFF0000FF);
    for (int frame = 0; frame < 4; ++frame) {
        canvas->queue_redraw({20, 20, 40, 40});
        REQUIRE(app.event_loop().poll());
    }
    const auto path =
        window.inspector().last_frame_diagnostics().render_hotspot_counters.gpu_present_path;
    CHECK(path != nk::GpuPresentPath::None);
    CHECK(path != nk::GpuPresentPath::SoftwareDirect);
    CHECK(path != nk::GpuPresentPath::SoftwareUpload);
    const auto shot = window.inspector().capture_debug_screenshot();
    REQUIRE(shot);
    REQUIRE(shot->source_backend == nk::RendererBackend::Vulkan);
    const auto a = canvas->allocation();
    check_rgb(*shot, {a.x + 40, a.y + 40}, window.scale_factor(), {0, 0, 0});
    check_rgb(*shot, {a.x + 100, a.y + 40}, window.scale_factor(), {0, 0, 255});
    CHECK(connection.connected());
#else
    SKIP("Fault interposer is Linux-specific");
#endif
}

TEST_CASE("Vulkan evicts cold image allocations within its byte budget", "[vulkan][render]") {
    if (!native_vulkan_available()) {
        SKIP("Requires a native Vulkan surface");
    }
    VulkanEnvironment environment;
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Vulkan cache budget", .width = 240, .height = 180});
    // Keep source addresses distinct while exercising eviction by bytes well
    // before the entry-count limit. Only one large image is visible at a time.
    std::vector<std::vector<uint32_t>> images(26);
    for (std::size_t i = 0; i < images.size(); ++i) {
        images[i].resize(1024U * 1024U, 0xFF000000U | (static_cast<uint32_t>(i) << 16));
    }
    std::size_t current = 0;
    auto canvas = nk::CanvasWidget::create();
    auto connection = canvas->on_draw().connect([&](nk::SnapshotContext& ctx, nk::Rect a) {
        ctx.add_image(a, images[current].data(), 1024, 1024);
    });
    window.set_child(canvas);
    window.present();
    REQUIRE(app.event_loop().poll());
    for (std::size_t frame = 1; frame <= images.size(); ++frame) {
        current = frame % images.size();
        canvas->queue_redraw();
        REQUIRE(app.event_loop().poll());
        const auto counters = window.inspector().last_frame_diagnostics().render_hotspot_counters;
        REQUIRE(counters.gpu_present_path != nk::GpuPresentPath::SoftwareUpload);
        CHECK(counters.texture_cache_byte_count <= 96ULL * 1024 * 1024);
        CHECK(counters.texture_allocation_count >= 1);
        CHECK(counters.image_texture_upload_count == 1);
    }
    const auto shot = window.inspector().capture_debug_screenshot();
    REQUIRE(shot);
    REQUIRE(shot->source_backend == nk::RendererBackend::Vulkan);
    const auto a = canvas->allocation();
    check_rgb(*shot, {a.x + 40, a.y + 40}, window.scale_factor(), {0, 0, 0});
    CHECK(connection.connected());
}
