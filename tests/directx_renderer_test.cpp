#include "../src/render/directx_device.h"

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstdlib>
#include <nk/platform/platform_backend.h>
#include <nk/render/render_node.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/text_shaper.h>
#include <windows.h>

namespace {

class DirectXSurface final : public nk::NativeSurface {
public:
    explicit DirectXSurface(float scale) : scale_(scale) {
        window_ = CreateWindowExW(0,
                                  L"STATIC",
                                  L"DirectX pixel tests",
                                  WS_OVERLAPPEDWINDOW,
                                  0,
                                  0,
                                  400,
                                  300,
                                  nullptr,
                                  nullptr,
                                  GetModuleHandleW(nullptr),
                                  nullptr);
        REQUIRE(window_ != nullptr);
    }

    ~DirectXSurface() override { DestroyWindow(window_); }

    void show() override {}

    void hide() override {}

    void set_title(std::string_view) override {}

    void resize(int width, int height) override { size_ = {float(width), float(height)}; }

    nk::Size size() const override { return size_; }

    float scale_factor() const override { return scale_; }

    void set_scale(float scale) { scale_ = scale; }

    void present(const uint8_t*, int, int, std::span<const nk::Rect>) override {
        ++software_presents;
    }

    void set_fullscreen(bool) override {}

    bool is_fullscreen() const override { return false; }

    nk::NativeWindowHandle native_handle() const override { return window_; }

    void set_cursor_shape(nk::CursorShape) override {}

    int software_presents = 0;

private:
    HWND window_ = nullptr;
    nk::Size size_{240, 180};
    float scale_;
};

bool available(nk::RendererBackend backend) {
    const bool result = nk::renderer_backend_available(backend);
    if (std::getenv("NK_REQUIRE_DIRECTX") != nullptr) {
        REQUIRE(result);
    }
    return result;
}

nk::FramePixels draw(nk::Renderer& renderer,
                     DirectXSurface& surface,
                     const nk::RenderNode& root,
                     std::span<const nk::Rect> damage = {}) {
    renderer.begin_frame(surface.size(), surface.scale_factor());
    renderer.set_damage_regions(damage);
    renderer.render(root);
    renderer.end_frame();
    renderer.present(surface);
    const auto pixels = renderer.read_back_frame();
    REQUIRE(pixels.has_value());
    REQUIRE(surface.software_presents == 0);
    return *pixels;
}

nk::FramePixels
reference(DirectXSurface& surface, const nk::RenderNode& root, nk::TextShaper* shaper = nullptr) {
    nk::SoftwareRenderer renderer;
    renderer.set_text_shaper(shaper);
    renderer.begin_frame(surface.size(), surface.scale_factor());
    renderer.render(root);
    renderer.end_frame();
    return *renderer.read_back_frame();
}

void check_pixel(const nk::FramePixels& pixels,
                 float scale,
                 nk::Point point,
                 std::array<int, 3> expected,
                 int tolerance = 2) {
    const auto x = static_cast<int>(point.x * scale);
    const auto y = static_cast<int>(point.y * scale);
    REQUIRE(x >= 0);
    REQUIRE(y >= 0);
    REQUIRE(x < pixels.width);
    REQUIRE(y < pixels.height);
    const auto offset = (static_cast<std::size_t>(y) * pixels.width + x) * 4;
    for (std::size_t channel = 0; channel < 3; ++channel) {
        CAPTURE(x, y, channel, expected[channel], pixels.rgba[offset + channel]);
        CHECK(std::abs(int(pixels.rgba[offset + channel]) - expected[channel]) <= tolerance);
    }
}

void compare(const nk::FramePixels& actual,
             const nk::FramePixels& expected,
             double max_different_fraction = 0.01) {
    REQUIRE(actual.width == expected.width);
    REQUIRE(actual.height == expected.height);
    REQUIRE(actual.rgba.size() == expected.rgba.size());
    std::size_t different = 0;
    for (std::size_t i = 0; i < actual.rgba.size(); i += 4) {
        bool pixel_differs = false;
        for (std::size_t c = 0; c < 4; ++c) {
            pixel_differs |= std::abs(int(actual.rgba[i + c]) - int(expected.rgba[i + c])) > 4;
        }
        different += pixel_differs;
    }
    CAPTURE(different, actual.width, actual.height);
    CHECK(double(different) / (actual.width * actual.height) <= max_different_fraction);
}

} // namespace

TEST_CASE("DirectX presentation releases buffers and recovers from a removed D3D12 device",
          "[directx][render][recovery]") {
    if (!available(nk::RendererBackend::D3D12)) {
        SKIP("Requires D3D12 hardware");
    }
    DirectXSurface surface(1);
    nk::DirectXDevice gpu;
    const auto window = static_cast<HWND>(surface.native_handle());
    REQUIRE(gpu.create(window, true));
    REQUIRE(gpu.resize(240, 180));
    REQUIRE(gpu.acquire_back_buffer() != nullptr);
    REQUIRE(gpu.present({}));
    REQUIRE(gpu.resize(300, 200));
    REQUIRE(gpu.acquire_back_buffer() != nullptr);
    REQUIRE(gpu.present({}));
    Microsoft::WRL::ComPtr<ID3D11On12Device1> interop;
    REQUIRE(SUCCEEDED(gpu.device.As(&interop)));
    Microsoft::WRL::ComPtr<ID3D12Device5> device12;
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif
    const auto result = interop->GetD3D12Device(__uuidof(ID3D12Device5),
                                                reinterpret_cast<void**>(device12.GetAddressOf()));
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    REQUIRE(SUCCEEDED(result));
    device12->RemoveDevice();
    CHECK_FALSE(gpu.healthy());
    CHECK(gpu.acquire_back_buffer() == nullptr);
    device12.Reset();
    interop.Reset();
    gpu.reset();
    REQUIRE(gpu.create(window, true));
    REQUIRE(gpu.resize(240, 180));
    REQUIRE(gpu.healthy());
    REQUIRE(gpu.acquire_back_buffer() != nullptr);
    REQUIRE(gpu.present({}));
    CHECK_FALSE(gpu.validation_failed());
}

TEST_CASE("DirectX renders effects, paths, clipping and per-draw opacity on the GPU",
          "[directx][render]") {
    const auto backend = GENERATE(nk::RendererBackend::D3D11, nk::RendererBackend::D3D12);
    const float scale = GENERATE(1.0F, 1.25F, 2.0F);
    CAPTURE(nk::renderer_backend_name(backend), scale);
    if (!available(backend)) {
        SKIP("Requires DirectX hardware");
    }
    DirectXSurface surface(scale);
    auto renderer = nk::create_renderer(backend);
    REQUIRE(renderer->attach_surface(surface));
    nk::SnapshotContext ctx;
    const auto black = nk::Color::from_rgb(0, 0, 0);
    ctx.add_color_rect({0, 0, 240, 180}, nk::Color::from_rgb(255, 255, 255));
    ctx.add_rounded_rect({10, 10, 30, 30}, black, 6);
    ctx.add_border({50, 10, 30, 30}, black, 3, 5);
    ctx.add_linear_gradient({90, 10, 30, 30}, black, nk::Color::from_rgb(255, 255, 255));
    ctx.add_shadow({140, 10, 30, 30}, black, 0, 0, 4, 0, 5);
    ctx.add_line({15, 60}, {45, 90}, black, 4);
    ctx.add_line({65, 75}, {65, 75}, black, 6);
    ctx.add_path(
        {.points = {{90, 60}, {120, 90}, {135, 60}}, .closed = true}, nk::Color{0, 0, 0, 0.5F}, 5);
    ctx.push_opacity({150, 60, 70, 40}, 0.5F);
    ctx.add_color_rect({150, 60, 40, 40}, black);
    ctx.add_color_rect({170, 60, 40, 40}, black);
    ctx.pop_container();
    ctx.push_rounded_clip({10, 110, 40, 40}, 8);
    ctx.add_color_rect({0, 100, 60, 60}, black);
    ctx.pop_container();
    ctx.push_opacity({70, 110, 40, 40}, 0.5F);
    ctx.push_opacity({70, 110, 40, 40}, 0.5F);
    ctx.add_color_rect({70, 110, 40, 40}, black);
    ctx.pop_container();
    ctx.pop_container();
    const uint32_t invisible = 0xFFFF0000;
    ctx.push_rounded_clip({0, 0, 0, 0}, 0);
    ctx.add_image({0, 0, 240, 180}, &invisible, 1, 1);
    ctx.pop_container();
    auto root = ctx.take_root();
    const auto pixels = draw(*renderer, surface, *root);
    REQUIRE(renderer->last_hotspot_counters().gpu_draw_call_count > 0);
    REQUIRE(renderer->last_hotspot_counters().gpu_present_path ==
            nk::GpuPresentPath::FullRedrawCopyBack);
    check_pixel(pixels, scale, {25, 25}, {0, 0, 0});
    check_pixel(pixels, scale, {30, 75}, {0, 0, 0});
    check_pixel(pixels, scale, {65, 75}, {0, 0, 0});
    check_pixel(pixels, scale, {160, 25}, {0, 0, 0});
    check_pixel(pixels, scale, {180, 80}, {64, 64, 64});
    check_pixel(pixels, scale, {90, 130}, {191, 191, 191});
    compare(pixels, reference(surface, *root));
}

TEST_CASE("DirectX preserves affine transforms, large paths and deep clipping through fallback",
          "[directx][render]") {
    const auto backend = GENERATE(nk::RendererBackend::D3D11, nk::RendererBackend::D3D12);
    const int scenario = GENERATE(0, 1, 2);
    const float scale = GENERATE(1.0F, 1.5F);
    CAPTURE(nk::renderer_backend_name(backend), scenario, scale);
    if (!available(backend)) {
        SKIP("Requires DirectX hardware");
    }
    DirectXSurface surface(scale);
    auto renderer = nk::create_renderer(backend);
    REQUIRE(renderer->attach_surface(surface));
    nk::SnapshotContext ctx;
    ctx.add_color_rect({0, 0, 240, 180}, nk::Color::from_rgb(255, 255, 255));
    if (scenario == 0) {
        ctx.push_transform({.m11 = 0.75F, .m12 = 0.25F, .m21 = -0.2F, .m31 = 80, .m32 = 20});
        ctx.add_color_rect({0, 0, 50, 40}, nk::Color::from_rgb(0, 0, 0));
        ctx.pop_transform();
    } else if (scenario == 1) {
        nk::Path2D path;
        for (int i = 0; i < 160; ++i) {
            path.points.push_back({20 + float(i), 80 + 30 * std::sin(float(i) * 0.1F)});
        }
        ctx.add_path(std::move(path), nk::Color{0, 0, 0, 0.5F}, 5);
    } else {
        for (int i = 0; i < 10; ++i) {
            ctx.push_rounded_clip({20, 20, 100, 100}, 5);
        }
        ctx.add_color_rect({0, 0, 200, 160}, nk::Color::from_rgb(0, 0, 0));
        for (int i = 0; i < 10; ++i) {
            ctx.pop_container();
        }
    }
    auto root = ctx.take_root();
    const auto pixels = draw(*renderer, surface, *root);
    REQUIRE(renderer->last_hotspot_counters().gpu_present_path ==
            nk::GpuPresentPath::SoftwareUpload);
    compare(pixels, reference(surface, *root), 0);
}

TEST_CASE("DirectX preserves pixels across damage, fallback transitions, resize and scale changes",
          "[directx][render]") {
    const auto backend = GENERATE(nk::RendererBackend::D3D11, nk::RendererBackend::D3D12);
    CAPTURE(nk::renderer_backend_name(backend));
    if (!available(backend)) {
        SKIP("Requires DirectX hardware");
    }
    DirectXSurface surface(1);
    auto renderer = nk::create_renderer(backend);
    REQUIRE(renderer->attach_surface(surface));
    const std::array<nk::Rect, 1> damage{{{10, 10, 30, 30}}};
    for (int frame = 0; frame < 12; ++frame) {
        if (frame == 5) {
            surface.resize(300, 210);
        }
        if (frame == 8) {
            surface.set_scale(1.5F);
        }
        nk::SnapshotContext ctx;
        const auto size = surface.size();
        ctx.add_color_rect({0, 0, size.width, size.height}, nk::Color::from_rgb(255, 255, 255));
        if (frame == 2 || frame == 3) {
            ctx.push_transform({.m31 = 150, .m32 = 70});
            ctx.add_color_rect({0, 0, 30, 30}, nk::Color::from_rgb(0, 0, 255));
            ctx.pop_transform();
        }
        ctx.add_color_rect({10, 10, 30, 30},
                           frame % 2 ? nk::Color::from_rgb(255, 0, 0)
                                     : nk::Color::from_rgb(0, 255, 0));
        auto root = ctx.take_root();
        const auto pixels =
            draw(*renderer,
                 surface,
                 *root,
                 frame == 0 ? std::span<const nk::Rect>{} : std::span<const nk::Rect>{damage});
        CAPTURE(frame);
        compare(pixels, reference(surface, *root), 0);
    }
}

TEST_CASE("DirectX caches text and images and detects pixels changed at the same address",
          "[directx][render]") {
    const auto backend = GENERATE(nk::RendererBackend::D3D11, nk::RendererBackend::D3D12);
    CAPTURE(nk::renderer_backend_name(backend));
    if (!available(backend)) {
        SKIP("Requires DirectX hardware");
    }
    DirectXSurface surface(1.5F);
    auto shaper = nk::TextShaper::create();
    auto renderer = nk::create_renderer(backend);
    REQUIRE(renderer->attach_surface(surface));
    renderer->set_text_shaper(shaper.get());
    std::array<uint32_t, 16> image{};
    image.fill(0xFFFF0000);
    for (int frame = 0; frame < 3; ++frame) {
        if (frame == 2) {
            image.fill(0xFF0000FF);
        }
        nk::SnapshotContext ctx(shaper.get());
        ctx.add_color_rect({0, 0, 240, 180}, nk::Color::from_rgb(255, 255, 255));
        ctx.push_opacity({10, 10, 60, 60}, 0.5F);
        ctx.add_image({10, 10, 60, 60}, image.data(), 4, 4);
        ctx.pop_container();
        ctx.add_wrapped_text({10, 90},
                             "DirectX text wraps over several lines",
                             nk::Color{0, 0, 0, 0.7F},
                             {.family = "System", .size = 14},
                             100);
        auto root = ctx.take_root();
        const auto pixels = draw(*renderer, surface, *root);
        REQUIRE(renderer->last_hotspot_counters().gpu_draw_call_count > 0);
        if (frame == 1) {
            CHECK(renderer->last_hotspot_counters().image_texture_upload_count == 0);
            CHECK(renderer->last_hotspot_counters().image_texture_cache_hit_count > 0);
            CHECK(renderer->last_hotspot_counters().text_texture_cache_hit_count > 0);
        }
        check_pixel(pixels,
                    surface.scale_factor(),
                    {30, 30},
                    frame == 2 ? std::array<int, 3>{127, 127, 255}
                               : std::array<int, 3>{255, 127, 127});
        compare(pixels, reference(surface, *root, shaper.get()));
    }
}

TEST_CASE("DirectX partial redraw does not blend translucent images again between damage regions",
          "[directx][render][damage]") {
    const auto backend = GENERATE(nk::RendererBackend::D3D11, nk::RendererBackend::D3D12);
    CAPTURE(nk::renderer_backend_name(backend));
    if (!available(backend)) {
        SKIP("Requires DirectX hardware");
    }
    DirectXSurface surface(1);
    auto renderer = nk::create_renderer(backend);
    REQUIRE(renderer->attach_surface(surface));
    const uint32_t image = 0x80000000;
    nk::SnapshotContext ctx;
    ctx.add_color_rect({0, 0, 240, 180}, nk::Color::from_rgb(255, 255, 255));
    ctx.add_image({10, 10, 120, 80}, &image, 1, 1);
    auto root = ctx.take_root();
    const auto expected = reference(surface, *root);
    compare(draw(*renderer, surface, *root), expected, 0);
    const std::array<nk::Rect, 2> damage{{{20, 20, 20, 20}, {44, 20, 20, 20}}};
    for (int frame = 0; frame < 3; ++frame) {
        compare(draw(*renderer, surface, *root, damage), expected, 0);
    }
}
