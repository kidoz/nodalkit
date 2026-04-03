/// @file renderer_test.cpp
/// @brief Renderer regression coverage for logical-to-device scaling.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <nk/platform/platform_backend.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>

namespace {

struct Pixel {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 0;
};

Pixel pixel_at(const nk::SoftwareRenderer& renderer, int x, int y) {
    const auto* data = renderer.pixel_data();
    const auto index = static_cast<std::size_t>((y * renderer.pixel_width() + x) * 4);
    return {
        data[index + 0],
        data[index + 1],
        data[index + 2],
        data[index + 3],
    };
}

class RecordingSurface final : public nk::NativeSurface {
public:
    void show() override {}

    void hide() override {}

    void set_title(std::string_view /*title*/) override {}

    void resize(int width, int height) override {
        size_ = {static_cast<float>(width), static_cast<float>(height)};
    }

    [[nodiscard]] nk::Size size() const override { return size_; }

    [[nodiscard]] float scale_factor() const override { return 1.0F; }

    void present(const uint8_t* /*rgba*/,
                 int w,
                 int h,
                 std::span<const nk::Rect> damage_regions) override {
        present_width = w;
        present_height = h;
        present_regions.assign(damage_regions.begin(), damage_regions.end());
    }

    void set_fullscreen(bool /*fullscreen*/) override {}

    [[nodiscard]] bool is_fullscreen() const override { return false; }

    [[nodiscard]] nk::NativeWindowHandle native_handle() const override { return nullptr; }

    void set_cursor_shape(nk::CursorShape /*shape*/) override {}

    nk::Size size_{};
    int present_width = 0;
    int present_height = 0;
    std::vector<nk::Rect> present_regions;
};

} // namespace

TEST_CASE("SoftwareRenderer scales logical content into a HiDPI framebuffer", "[renderer]") {
    nk::SnapshotContext snapshot;
    snapshot.add_color_rect({2.0F, 3.0F, 4.0F, 5.0F}, nk::Color::from_rgb(255, 0, 0));

    auto root = snapshot.take_root();
    REQUIRE(root != nullptr);

    nk::SoftwareRenderer renderer;
    renderer.begin_frame({20.0F, 20.0F}, 2.0F);
    renderer.render(*root);
    renderer.end_frame();

    REQUIRE(renderer.pixel_width() == 40);
    REQUIRE(renderer.pixel_height() == 40);

    const auto inside = pixel_at(renderer, 8, 10);
    REQUIRE(inside.r == 255);
    REQUIRE(inside.g == 0);
    REQUIRE(inside.b == 0);
    REQUIRE(inside.a == 255);

    const auto outside = pixel_at(renderer, 20, 20);
    REQUIRE(outside.r == 255);
    REQUIRE(outside.g == 255);
    REQUIRE(outside.b == 255);
    REQUIRE(outside.a == 255);
}

TEST_CASE("SoftwareRenderer preserves undamaged pixels across partial redraws", "[renderer]") {
    nk::SnapshotContext first_snapshot;
    first_snapshot.add_color_rect({0.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(255, 0, 0));
    first_snapshot.add_color_rect({10.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(0, 0, 255));
    auto first_root = first_snapshot.take_root();
    REQUIRE(first_root != nullptr);

    nk::SoftwareRenderer renderer;
    renderer.begin_frame({20.0F, 10.0F}, 1.0F);
    renderer.render(*first_root);
    renderer.end_frame();

    const auto first_left = pixel_at(renderer, 4, 5);
    const auto first_right = pixel_at(renderer, 15, 5);
    REQUIRE(first_left.r == 255);
    REQUIRE(first_left.g == 0);
    REQUIRE(first_left.b == 0);
    REQUIRE(first_right.r == 0);
    REQUIRE(first_right.g == 0);
    REQUIRE(first_right.b == 255);

    nk::SnapshotContext second_snapshot;
    second_snapshot.add_color_rect({0.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(0, 255, 0));
    auto second_root = second_snapshot.take_root();
    REQUIRE(second_root != nullptr);

    renderer.begin_frame({20.0F, 10.0F}, 1.0F);
    const std::array<nk::Rect, 1> damage_regions = {nk::Rect{0.0F, 0.0F, 10.0F, 10.0F}};
    renderer.set_damage_regions(damage_regions);
    renderer.render(*second_root);
    renderer.end_frame();

    const auto second_left = pixel_at(renderer, 4, 5);
    const auto second_right = pixel_at(renderer, 15, 5);
    REQUIRE(second_left.r == 0);
    REQUIRE(second_left.g == 255);
    REQUIRE(second_left.b == 0);
    REQUIRE(second_right.r == 0);
    REQUIRE(second_right.g == 0);
    REQUIRE(second_right.b == 255);
    REQUIRE(renderer.last_hotspot_counters().damage_region_count == 1);
}

TEST_CASE("SoftwareRenderer forwards partial present regions to native surfaces", "[renderer]") {
    nk::SnapshotContext first_snapshot;
    first_snapshot.add_color_rect({0.0F, 0.0F, 12.0F, 12.0F}, nk::Color::from_rgb(255, 0, 0));
    auto first_root = first_snapshot.take_root();
    REQUIRE(first_root != nullptr);

    nk::SoftwareRenderer renderer;
    RecordingSurface surface;
    renderer.begin_frame({24.0F, 12.0F}, 1.0F);
    renderer.render(*first_root);
    renderer.end_frame();
    renderer.present(surface);
    REQUIRE(surface.present_regions.empty());
    REQUIRE(renderer.last_hotspot_counters().gpu_present_region_count == 0);

    nk::SnapshotContext second_snapshot;
    second_snapshot.add_color_rect({0.0F, 0.0F, 6.0F, 12.0F}, nk::Color::from_rgb(0, 255, 0));
    auto second_root = second_snapshot.take_root();
    REQUIRE(second_root != nullptr);

    renderer.begin_frame({24.0F, 12.0F}, 1.0F);
    const std::array<nk::Rect, 1> damage_regions = {nk::Rect{0.0F, 0.0F, 6.0F, 12.0F}};
    renderer.set_damage_regions(damage_regions);
    renderer.render(*second_root);
    renderer.end_frame();
    renderer.present(surface);

    REQUIRE(surface.present_width == 24);
    REQUIRE(surface.present_height == 12);
    REQUIRE(surface.present_regions.size() == 1);
    REQUIRE(surface.present_regions.front() == damage_regions.front());
    REQUIRE(renderer.last_hotspot_counters().gpu_present_region_count == 1);
    REQUIRE(renderer.last_hotspot_counters().gpu_present_path ==
            nk::GpuPresentPath::SoftwareUpload);
}
