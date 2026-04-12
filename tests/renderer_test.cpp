/// @file renderer_test.cpp
/// @brief Renderer regression coverage for logical-to-device scaling.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <nk/platform/platform_backend.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/text_shaper.h>

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

    [[nodiscard]] float scale_factor() const override { return scale_factor_; }

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
    float scale_factor_ = 1.0F;
    int present_width = 0;
    int present_height = 0;
    std::vector<nk::Rect> present_regions;
};

class RecordingTextShaper final : public nk::TextShaper {
public:
    [[nodiscard]] nk::Size measure(std::string_view /*text*/,
                                   nk::FontDescriptor const& font) const override {
        last_measured_font_size = font.size;
        return {font.size, font.size};
    }

    [[nodiscard]] nk::ShapedText shape(std::string_view /*text*/,
                                       nk::FontDescriptor const& font,
                                       nk::Color color) const override {
        last_shaped_font_size = font.size;
        nk::ShapedText shaped;
        shaped.set_text_size({font.size, font.size});
        shaped.set_baseline(font.size * 0.8F);
        const auto red = static_cast<uint8_t>(color.r * 255.0F);
        const auto green = static_cast<uint8_t>(color.g * 255.0F);
        const auto blue = static_cast<uint8_t>(color.b * 255.0F);
        shaped.set_bitmap({red, green, blue, 255}, 1, 1);
        return shaped;
    }

    mutable float last_measured_font_size = 0.0F;
    mutable float last_shaped_font_size = 0.0F;
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

TEST_CASE("NativeSurface derives framebuffer size from logical size and scale factor",
          "[renderer]") {
    RecordingSurface surface;
    surface.size_ = {123.0F, 45.0F};
    surface.scale_factor_ = 1.5F;

    const auto framebuffer = surface.framebuffer_size();
    REQUIRE(framebuffer.width == 185.0F);
    REQUIRE(framebuffer.height == 68.0F);
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

TEST_CASE("SoftwareRenderer forwards logical font sizes to text shaping on Windows", "[renderer]") {
    nk::SnapshotContext snapshot;
    nk::FontDescriptor font{
        .family = "System",
        .size = 12.0F,
        .weight = nk::FontWeight::Regular,
    };
    snapshot.add_text({2.0F, 3.0F}, "Scale", nk::Color::from_rgb(0, 0, 0), font);
    auto root = snapshot.take_root();
    REQUIRE(root != nullptr);

    nk::SoftwareRenderer renderer;
    RecordingTextShaper text_shaper;
    renderer.set_text_shaper(&text_shaper);
    renderer.begin_frame({40.0F, 20.0F}, 2.0F);
    renderer.render(*root);
    renderer.end_frame();

#if defined(_WIN32)
    REQUIRE(std::fabs(text_shaper.last_shaped_font_size - 12.0F) < 0.01F);
#else
    REQUIRE(std::fabs(text_shaper.last_shaped_font_size - 24.0F) < 0.01F);
#endif
}

namespace {

const nk::RenderNode* find_text_node(const nk::RenderNode& node) {
    if (node.kind() == nk::RenderNodeKind::Text) {
        return &node;
    }
    for (const auto& child : node.children()) {
        if (const auto* found = find_text_node(*child)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("ShadowNode renders a blurred outset shadow in software", "[renderer][shadow]") {
    nk::SnapshotContext snapshot;
    // Black shadow with 4px blur, no offset, behind a 10×10 rect centered at (10,10).
    snapshot.add_shadow({10.0F, 10.0F, 10.0F, 10.0F},
                        nk::Color{0.0F, 0.0F, 0.0F, 1.0F},
                        0.0F, 0.0F, 4.0F);
    auto root = snapshot.take_root();
    REQUIRE(root != nullptr);

    nk::SoftwareRenderer renderer;
    renderer.begin_frame({30.0F, 30.0F}, 1.0F);
    renderer.render(*root);
    renderer.end_frame();

    // Inside the shadow rect core: should be dark (blended black onto white).
    const auto inside = pixel_at(renderer, 14, 14);
    REQUIRE(inside.r < 30);
    REQUIRE(inside.g < 30);
    REQUIRE(inside.b < 30);

    // At the blur edge (4px outside the shadow rect): should be partially faded.
    const auto edge = pixel_at(renderer, 7, 14);
    REQUIRE(edge.r > 100);
    REQUIRE(edge.r < 255);

    // Far outside: untouched white background.
    const auto outside = pixel_at(renderer, 2, 2);
    REQUIRE(outside.r == 255);
    REQUIRE(outside.g == 255);
    REQUIRE(outside.b == 255);
}

TEST_CASE("LinearGradientNode renders a vertical color ramp in software", "[renderer][gradient]") {
    nk::SnapshotContext snapshot;
    // Red at top → blue at bottom, 20×20 rect.
    snapshot.add_linear_gradient({0.0F, 0.0F, 20.0F, 20.0F},
                                 nk::Color{1.0F, 0.0F, 0.0F, 1.0F},
                                 nk::Color{0.0F, 0.0F, 1.0F, 1.0F},
                                 nk::Orientation::Vertical);
    auto root = snapshot.take_root();
    REQUIRE(root != nullptr);

    nk::SoftwareRenderer renderer;
    renderer.begin_frame({20.0F, 20.0F}, 1.0F);
    renderer.render(*root);
    renderer.end_frame();

    // Top row: mostly red.
    const auto top = pixel_at(renderer, 10, 1);
    REQUIRE(top.r > 200);
    REQUIRE(top.b < 55);

    // Bottom row: mostly blue.
    const auto bottom = pixel_at(renderer, 10, 18);
    REQUIRE(bottom.r < 55);
    REQUIRE(bottom.b > 200);

    // Middle: roughly equal red and blue (~127 each).
    const auto mid = pixel_at(renderer, 10, 10);
    REQUIRE(mid.r >= 100);
    REQUIRE(mid.r <= 155);
    REQUIRE(mid.b >= 100);
    REQUIRE(mid.b <= 155);
}

TEST_CASE("SnapshotContext::add_text measures text bounds when a shaper is provided",
          "[renderer][text-bounds]") {
    RecordingTextShaper shaper;
    nk::SnapshotContext snapshot{&shaper};
    nk::FontDescriptor font{
        .family = "System",
        .size = 18.0F,
        .weight = nk::FontWeight::Regular,
    };

    snapshot.add_text({5.0F, 7.0F}, "Hello", nk::Color::from_rgb(0, 0, 0), font);

    auto root = snapshot.take_root();
    REQUIRE(root != nullptr);
    const auto* text_node = find_text_node(*root);
    REQUIRE(text_node != nullptr);

    // RecordingTextShaper::measure returns {font.size, font.size}, so with
    // a 18pt font we expect bounds {5, 7, 18, 18}.
    const auto& bounds = text_node->bounds();
    REQUIRE(bounds.x == 5.0F);
    REQUIRE(bounds.y == 7.0F);
    REQUIRE(bounds.width == 18.0F);
    REQUIRE(bounds.height == 18.0F);
}

TEST_CASE("OpacityNode multiplies child alpha in software rendering", "[renderer][opacity]") {
    nk::SnapshotContext snapshot;
    snapshot.push_opacity({0.0F, 0.0F, 20.0F, 20.0F}, 0.5F);
    snapshot.add_color_rect({2.0F, 2.0F, 4.0F, 4.0F}, nk::Color{1.0F, 0.0F, 0.0F, 1.0F});
    snapshot.pop_container();

    auto root = snapshot.take_root();
    REQUIRE(root != nullptr);

    nk::SoftwareRenderer renderer;
    renderer.begin_frame({20.0F, 20.0F}, 1.0F);
    renderer.render(*root);
    renderer.end_frame();

    // The red rect at (2,2)-(6,6) should be blended at 50% opacity onto
    // the white background. Expected: R = 255*0.5 + 255*0.5 = 255,
    // G = 0*0.5 + 255*0.5 = 127, B = same as G. Actually with premultiplied
    // alpha blending: dst = src*alpha + dst*(1-alpha).
    // src = (1,0,0,0.5), dst = (1,1,1,1) → result = (1, 0.5, 0.5, 1)
    // → (255, 127, 127, 255)
    const auto inside = pixel_at(renderer, 3, 3);
    REQUIRE(inside.r == 255);
    REQUIRE(inside.g >= 126);
    REQUIRE(inside.g <= 128);
    REQUIRE(inside.b >= 126);
    REQUIRE(inside.b <= 128);
    REQUIRE(inside.a == 255);

    // Outside the opacity rect, the background is untouched.
    const auto outside = pixel_at(renderer, 10, 10);
    REQUIRE(outside.r == 255);
    REQUIRE(outside.g == 255);
    REQUIRE(outside.b == 255);
}

TEST_CASE("SnapshotContext::add_text falls back to zero-size bounds without a shaper",
          "[renderer][text-bounds]") {
    nk::SnapshotContext snapshot;  // no shaper
    nk::FontDescriptor font{
        .family = "System",
        .size = 14.0F,
        .weight = nk::FontWeight::Regular,
    };

    snapshot.add_text({2.0F, 3.0F}, "Unmeasured", nk::Color::from_rgb(0, 0, 0), font);

    auto root = snapshot.take_root();
    REQUIRE(root != nullptr);
    const auto* text_node = find_text_node(*root);
    REQUIRE(text_node != nullptr);

    // Without a shaper, bounds preserve the origin but have zero size —
    // the documented fallback behavior. Callers that rely on text bounds
    // for hit testing or clipping must provide a shaper.
    const auto& bounds = text_node->bounds();
    REQUIRE(bounds.x == 2.0F);
    REQUIRE(bounds.y == 3.0F);
    REQUIRE(bounds.width == 0.0F);
    REQUIRE(bounds.height == 0.0F);
}

// --- Damage tracking edge case tests ---

TEST_CASE("SoftwareRenderer handles multiple disjoint damage regions", "[renderer]") {
    nk::SnapshotContext first_snap;
    first_snap.add_color_rect({0.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(255, 0, 0));
    first_snap.add_color_rect({10.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(0, 255, 0));
    first_snap.add_color_rect({20.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(0, 0, 255));
    auto first = first_snap.take_root();

    nk::SoftwareRenderer renderer;
    renderer.begin_frame({30.0F, 10.0F}, 1.0F);
    renderer.render(*first);
    renderer.end_frame();

    // Frame 2: damage left and right bands, change colors.
    nk::SnapshotContext second_snap;
    second_snap.add_color_rect({0.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(255, 255, 0));
    second_snap.add_color_rect({10.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(0, 255, 0));
    second_snap.add_color_rect({20.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(0, 255, 255));
    auto second = second_snap.take_root();

    renderer.begin_frame({30.0F, 10.0F}, 1.0F);
    const std::array<nk::Rect, 2> damage = {
        nk::Rect{0.0F, 0.0F, 10.0F, 10.0F},
        nk::Rect{20.0F, 0.0F, 10.0F, 10.0F},
    };
    renderer.set_damage_regions(damage);
    renderer.render(*second);
    renderer.end_frame();

    const auto left = pixel_at(renderer, 5, 5);
    const auto mid = pixel_at(renderer, 15, 5);
    const auto right = pixel_at(renderer, 25, 5);
    REQUIRE(left.r == 255);
    REQUIRE(left.g == 255);
    REQUIRE(left.b == 0);
    REQUIRE(mid.r == 0);
    REQUIRE(mid.g == 255);
    REQUIRE(mid.b == 0);
    REQUIRE(right.r == 0);
    REQUIRE(right.g == 255);
    REQUIRE(right.b == 255);
    REQUIRE(renderer.last_hotspot_counters().damage_region_count == 2);
}

TEST_CASE("SoftwareRenderer merges overlapping damage regions", "[renderer]") {
    nk::SoftwareRenderer renderer;
    renderer.begin_frame({30.0F, 10.0F}, 1.0F);
    const std::array<nk::Rect, 2> damage = {
        nk::Rect{0.0F, 0.0F, 15.0F, 10.0F},
        nk::Rect{10.0F, 0.0F, 15.0F, 10.0F},
    };
    renderer.set_damage_regions(damage);
    REQUIRE(renderer.last_hotspot_counters().damage_region_count == 1);
}

TEST_CASE("SoftwareRenderer treats zero-size damage as full redraw", "[renderer]") {
    nk::SnapshotContext snap;
    snap.add_color_rect({0.0F, 0.0F, 20.0F, 10.0F}, nk::Color::from_rgb(255, 0, 0));
    auto root = snap.take_root();

    nk::SoftwareRenderer renderer;
    renderer.begin_frame({20.0F, 10.0F}, 1.0F);
    renderer.render(*root);
    renderer.end_frame();

    renderer.begin_frame({20.0F, 10.0F}, 1.0F);
    const std::array<nk::Rect, 1> damage = {nk::Rect{5.0F, 5.0F, 0.0F, 0.0F}};
    renderer.set_damage_regions(damage);
    REQUIRE(renderer.last_hotspot_counters().damage_region_count == 0);
}

TEST_CASE("SoftwareRenderer falls back to full redraw with empty damage list", "[renderer]") {
    nk::SnapshotContext snap;
    snap.add_color_rect({0.0F, 0.0F, 20.0F, 10.0F}, nk::Color::from_rgb(128, 0, 0));
    auto root = snap.take_root();

    nk::SoftwareRenderer renderer;
    renderer.begin_frame({20.0F, 10.0F}, 1.0F);
    renderer.render(*root);
    renderer.end_frame();

    // Frame 2: empty damage = full redraw.
    nk::SnapshotContext snap2;
    snap2.add_color_rect({0.0F, 0.0F, 20.0F, 10.0F}, nk::Color::from_rgb(0, 128, 0));
    auto root2 = snap2.take_root();

    renderer.begin_frame({20.0F, 10.0F}, 1.0F);
    renderer.set_damage_regions({});
    renderer.render(*root2);
    renderer.end_frame();

    REQUIRE(renderer.last_hotspot_counters().damage_region_count == 0);
    const auto px = pixel_at(renderer, 10, 5);
    REQUIRE(px.r == 0);
    REQUIRE(px.g == 128);
}

TEST_CASE("SoftwareRenderer forces full redraw on viewport resize", "[renderer]") {
    nk::SnapshotContext snap;
    snap.add_color_rect({0.0F, 0.0F, 20.0F, 10.0F}, nk::Color::from_rgb(200, 0, 0));
    auto root = snap.take_root();

    nk::SoftwareRenderer renderer;
    renderer.begin_frame({20.0F, 10.0F}, 1.0F);
    renderer.render(*root);
    renderer.end_frame();
    REQUIRE(renderer.pixel_width() == 20);

    // Frame 2: resize viewport. Even with damage hint, full redraw should happen.
    nk::SnapshotContext snap2;
    snap2.add_color_rect({0.0F, 0.0F, 30.0F, 10.0F}, nk::Color::from_rgb(0, 200, 0));
    auto root2 = snap2.take_root();

    renderer.begin_frame({30.0F, 10.0F}, 1.0F);
    const std::array<nk::Rect, 1> damage = {nk::Rect{0.0F, 0.0F, 10.0F, 10.0F}};
    renderer.set_damage_regions(damage);
    renderer.render(*root2);
    renderer.end_frame();

    REQUIRE(renderer.pixel_width() == 30);
    // Pixel at x=25 should be green (full redraw happened despite small damage hint).
    const auto px = pixel_at(renderer, 25, 5);
    REQUIRE(px.g == 200);
}

TEST_CASE("SoftwareRenderer clamps damage region to viewport bounds", "[renderer]") {
    nk::SnapshotContext snap;
    snap.add_color_rect({0.0F, 0.0F, 20.0F, 10.0F}, nk::Color::from_rgb(100, 100, 100));
    auto root = snap.take_root();

    nk::SoftwareRenderer renderer;
    renderer.begin_frame({20.0F, 10.0F}, 1.0F);
    renderer.render(*root);
    renderer.end_frame();

    // Frame 2: damage extends well beyond viewport.
    nk::SnapshotContext snap2;
    snap2.add_color_rect({0.0F, 0.0F, 20.0F, 10.0F}, nk::Color::from_rgb(50, 50, 50));
    auto root2 = snap2.take_root();

    renderer.begin_frame({20.0F, 10.0F}, 1.0F);
    const std::array<nk::Rect, 1> damage = {nk::Rect{-5.0F, -5.0F, 30.0F, 20.0F}};
    renderer.set_damage_regions(damage);
    renderer.render(*root2);
    renderer.end_frame();

    // Should not crash and should still render correctly.
    REQUIRE(renderer.last_hotspot_counters().damage_region_count == 1);
    const auto px = pixel_at(renderer, 10, 5);
    REQUIRE(px.r == 50);
}

TEST_CASE("SoftwareRenderer preserves undamaged quadrants across consecutive partial redraws",
          "[renderer]") {
    // Frame 1: 4 quadrants with distinct colors.
    nk::SnapshotContext snap1;
    snap1.add_color_rect({0.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(255, 0, 0));     // TL red
    snap1.add_color_rect({10.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(0, 255, 0));     // TR green
    snap1.add_color_rect({0.0F, 10.0F, 10.0F, 10.0F}, nk::Color::from_rgb(0, 0, 255));     // BL blue
    snap1.add_color_rect({10.0F, 10.0F, 10.0F, 10.0F}, nk::Color::from_rgb(255, 255, 255)); // BR white
    auto root1 = snap1.take_root();

    nk::SoftwareRenderer renderer;
    renderer.begin_frame({20.0F, 20.0F}, 1.0F);
    renderer.render(*root1);
    renderer.end_frame();

    // Verify initial state.
    REQUIRE(pixel_at(renderer, 5, 5).r == 255);    // TL red
    REQUIRE(pixel_at(renderer, 15, 5).g == 255);    // TR green
    REQUIRE(pixel_at(renderer, 5, 15).b == 255);    // BL blue
    REQUIRE(pixel_at(renderer, 15, 15).r == 255);   // BR white

    // Frame 2: damage only TL quadrant, change to yellow.
    nk::SnapshotContext snap2;
    snap2.add_color_rect({0.0F, 0.0F, 10.0F, 10.0F}, nk::Color::from_rgb(255, 255, 0));
    auto root2 = snap2.take_root();

    renderer.begin_frame({20.0F, 20.0F}, 1.0F);
    const std::array<nk::Rect, 1> damage2 = {nk::Rect{0.0F, 0.0F, 10.0F, 10.0F}};
    renderer.set_damage_regions(damage2);
    renderer.render(*root2);
    renderer.end_frame();

    REQUIRE(pixel_at(renderer, 5, 5).r == 255);     // TL now yellow
    REQUIRE(pixel_at(renderer, 5, 5).g == 255);
    REQUIRE(pixel_at(renderer, 5, 5).b == 0);
    REQUIRE(pixel_at(renderer, 15, 5).g == 255);     // TR still green
    REQUIRE(pixel_at(renderer, 5, 15).b == 255);     // BL still blue
    REQUIRE(pixel_at(renderer, 15, 15).r == 255);    // BR still white

    // Frame 3: damage only BR quadrant, change to cyan.
    nk::SnapshotContext snap3;
    snap3.add_color_rect({10.0F, 10.0F, 10.0F, 10.0F}, nk::Color::from_rgb(0, 255, 255));
    auto root3 = snap3.take_root();

    renderer.begin_frame({20.0F, 20.0F}, 1.0F);
    const std::array<nk::Rect, 1> damage3 = {nk::Rect{10.0F, 10.0F, 10.0F, 10.0F}};
    renderer.set_damage_regions(damage3);
    renderer.render(*root3);
    renderer.end_frame();

    REQUIRE(pixel_at(renderer, 5, 5).r == 255);      // TL still yellow (from frame 2)
    REQUIRE(pixel_at(renderer, 5, 5).g == 255);
    REQUIRE(pixel_at(renderer, 15, 5).g == 255);      // TR still green (never touched)
    REQUIRE(pixel_at(renderer, 5, 15).b == 255);      // BL still blue (never touched)
    REQUIRE(pixel_at(renderer, 15, 15).r == 0);       // BR now cyan
    REQUIRE(pixel_at(renderer, 15, 15).g == 255);
    REQUIRE(pixel_at(renderer, 15, 15).b == 255);
    REQUIRE(renderer.last_hotspot_counters().damage_region_count == 1);
}
