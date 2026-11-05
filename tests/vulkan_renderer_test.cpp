#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstdlib>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/platform/window_inspector.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
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
