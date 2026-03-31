/// @file renderer_test.cpp
/// @brief Renderer regression coverage for logical-to-device scaling.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
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
