#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <nk/foundation/logging.h>
#include <nk/platform/platform_backend.h>
#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/shaped_text.h>
#include <nk/text/text_shaper.h>
#include <string>
#include <unordered_map>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wkeyword-macro"
#define private public
#define create_metal_renderer create_metal_renderer_under_test
#include "../src/render/metal_renderer.mm"
#undef create_metal_renderer
#undef private
#pragma clang diagnostic pop

@interface NKMetalTestCaptureLayer : CAMetalLayer
@property(nonatomic, retain) NSMutableArray* captured;
@end

@implementation NKMetalTestCaptureLayer

- (id<CAMetalDrawable>)nextDrawable {
    id<CAMetalDrawable> drawable = [super nextDrawable];
    if (drawable != nil) {
        [_captured addObject:drawable];
    }
    return drawable;
}

@end

@interface NKMetalTestNilLayer : CAMetalLayer
@end

@implementation NKMetalTestNilLayer

- (id<CAMetalDrawable>)nextDrawable {
    return nil;
}

@end

namespace {

class TestSurface final : public nk::NativeSurface {
public:
    TestSurface() {
        [NSApplication sharedApplication];
        window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 64, 64)
                                             styleMask:NSWindowStyleMaskBorderless
                                               backing:NSBackingStoreBuffered
                                                 defer:NO];
    }

    ~TestSurface() override { [window release]; }

    void show() override {}

    void hide() override {}

    void set_title(std::string_view) override {}

    void resize(int, int) override {}

    [[nodiscard]] nk::Size size() const override { return {64, 64}; }

    [[nodiscard]] float scale_factor() const override { return 1.0F; }

    void present(const uint8_t* pixels, int width, int height, std::span<const nk::Rect>) override {
        ++cpu_presents;
        cpu_width = width;
        cpu_height = height;
        cpu_pixels.clear();
        if (pixels != nullptr && width > 0 && height > 0) {
            const auto size =
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
            cpu_pixels.assign(pixels, pixels + size);
        }
    }

    void set_fullscreen(bool) override {}

    [[nodiscard]] bool is_fullscreen() const override { return false; }

    [[nodiscard]] nk::NativeWindowHandle native_handle() const override {
        return valid ? window : nullptr;
    }

    void set_cursor_shape(nk::CursorShape) override {}

    NSWindow* window = nil;
    bool valid = true;
    int cpu_presents = 0;
    int cpu_width = 0;
    int cpu_height = 0;
    std::vector<uint8_t> cpu_pixels;
};

NKMetalTestCaptureLayer* attach_capture_layer(TestSurface& surface) {
    auto* layer = [NKMetalTestCaptureLayer layer];
    layer.captured = [NSMutableArray array];
    layer.framebufferOnly = NO;
    surface.window.contentView.wantsLayer = YES;
    surface.window.contentView.layer = layer;
    return layer;
}

struct MetalFixture {
    MetalFixture() : layer(attach_capture_layer(surface)) {}

    TestSurface surface;
    nk::MetalRenderer renderer;
    NKMetalTestCaptureLayer* layer = nil;
};

std::unique_ptr<nk::RenderNode> solid(nk::Color color) {
    nk::SnapshotContext snapshot;
    snapshot.add_color_rect({0, 0, 64, 64}, color);
    return snapshot.take_root();
}

std::unique_ptr<nk::RenderNode> fallback_root(nk::Color color) {
    nk::SnapshotContext snapshot;
    for (int index = 0; index < 5; ++index) {
        snapshot.push_rounded_clip({0, 0, 64, 64}, 0);
    }
    snapshot.add_color_rect({0, 0, 64, 64}, color);
    for (int index = 0; index < 5; ++index) {
        snapshot.pop_container();
    }
    return snapshot.take_root();
}

void frame(nk::MetalRenderer& renderer,
           TestSurface& surface,
           const nk::RenderNode& root,
           std::span<const nk::Rect> damage = {}) {
    renderer.begin_frame({64, 64}, 1.0F);
    renderer.set_damage_regions(damage);
    renderer.render(root);
    renderer.end_frame();
    renderer.present(surface);
}

std::array<int, 4> read_pixel(nk::MetalRenderer& renderer, id<MTLTexture> texture, int x, int y) {
    if (texture == nil) {
        return {-1, -1, -1, -1};
    }
    const NSUInteger stride = ((texture.width * 4 + 255) / 256) * 256;
    id<MTLBuffer> buffer = [renderer.device_ newBufferWithLength:stride * texture.height
                                                         options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> command_buffer = [renderer.command_queue_ commandBuffer];
    id<MTLBlitCommandEncoder> encoder = [command_buffer blitCommandEncoder];
    [encoder copyFromTexture:texture
                     sourceSlice:0
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(texture.width, texture.height, 1)
                        toBuffer:buffer
               destinationOffset:0
          destinationBytesPerRow:stride
        destinationBytesPerImage:stride * texture.height];
    [encoder endEncoding];
    [command_buffer commit];
    [command_buffer waitUntilCompleted];
    const NSUInteger texture_x = static_cast<NSUInteger>(x) * texture.width / 64;
    const NSUInteger texture_y = static_cast<NSUInteger>(y) * texture.height / 64;
    const auto* pixels =
        static_cast<const uint8_t*>(buffer.contents) + texture_y * stride + texture_x * 4;
    const std::array<int, 4> result{pixels[2], pixels[1], pixels[0], pixels[3]};
    [buffer release];
    return result;
}

void check_pixel(nk::MetalRenderer& renderer,
                 id<MTLTexture> texture,
                 int x,
                 int y,
                 std::array<int, 4> expected) {
    const auto actual = read_pixel(renderer, texture, x, y);
    INFO("actual RGBA: " << actual[0] << ',' << actual[1] << ',' << actual[2] << ',' << actual[3]);
    CHECK(actual == expected);
}

id<MTLTexture> last_drawable_texture(NKMetalTestCaptureLayer* layer) {
    id<CAMetalDrawable> drawable = [layer.captured lastObject];
    return drawable.texture;
}

} // namespace

TEST_CASE("Metal software fallback preserves unsupported node semantics", "[metal][render]") {
    SECTION("opacity zero") {
        MetalFixture fixture;
        nk::SnapshotContext snapshot;
        snapshot.push_opacity({0, 0, 64, 64}, 0.0F);
        snapshot.add_color_rect({0, 0, 64, 64}, {1, 0, 0, 1});
        snapshot.pop_container();
        auto root = snapshot.take_root();
        frame(fixture.renderer, fixture.surface, *root);
        check_pixel(
            fixture.renderer, last_drawable_texture(fixture.layer), 5, 5, {255, 255, 255, 255});
    }

    SECTION("gradient") {
        MetalFixture fixture;
        nk::SnapshotContext snapshot;
        snapshot.add_linear_gradient(
            {0, 0, 64, 64}, {1, 0, 0, 1}, {0, 0, 1, 1}, nk::Orientation::Vertical);
        auto root = snapshot.take_root();
        frame(fixture.renderer, fixture.surface, *root);
        const auto pixel =
            read_pixel(fixture.renderer, last_drawable_texture(fixture.layer), 5, 32);
        CHECK(pixel[0] > 0);
        CHECK(pixel[0] < 255);
        CHECK(pixel[2] > 0);
    }

    SECTION("identity transform") {
        MetalFixture fixture;
        nk::SnapshotContext snapshot;
        snapshot.push_transform({1, 0, 0, 1, 0, 0});
        snapshot.add_color_rect({0, 0, 64, 64}, {1, 0, 0, 1});
        snapshot.pop_transform();
        auto root = snapshot.take_root();
        frame(fixture.renderer, fixture.surface, *root);
        check_pixel(fixture.renderer, last_drawable_texture(fixture.layer), 5, 5, {255, 0, 0, 255});
    }

    SECTION("path") {
        MetalFixture fixture;
        nk::SnapshotContext snapshot;
        snapshot.add_path({{{0, 0}, {64, 64}}, false}, {1, 0, 0, 1}, 2.0F);
        auto root = snapshot.take_root();
        frame(fixture.renderer, fixture.surface, *root);
        const auto pixel =
            read_pixel(fixture.renderer, last_drawable_texture(fixture.layer), 32, 32);
        CHECK(pixel[0] == 255);
        CHECK(pixel[1] == 0);
        CHECK(pixel[2] == 0);
    }

    SECTION("shadow") {
        MetalFixture fixture;
        nk::SnapshotContext snapshot;
        snapshot.add_shadow({20, 20, 10, 10}, {0, 0, 0, 1}, 0, 0, 4.0F);
        auto root = snapshot.take_root();
        frame(fixture.renderer, fixture.surface, *root);
        REQUIRE(fixture.renderer.needs_software_fallback_);
        const auto pixel =
            read_pixel(fixture.renderer, last_drawable_texture(fixture.layer), 24, 24);
        CHECK(pixel[0] < 30);
        CHECK(pixel[1] < 30);
        CHECK(pixel[2] < 30);
    }
}

TEST_CASE("Metal wraps text and keys shaped text by width", "[metal][text]") {
    nk::MetalRenderer renderer;
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);
    renderer.set_text_shaper(shaper.get());

    nk::TextNode narrow(
        {0, 0}, {40, 60}, "This text should wrap across several lines", {0, 0, 0, 1}, {}, 40);
    renderer.begin_frame({64, 64}, 1.0F);
    renderer.render(narrow);
    REQUIRE(renderer.text_commands_.size() == 1);
    const auto narrow_bitmap = renderer.text_commands_[0].shaped_text;
    REQUIRE(narrow_bitmap != nullptr);
    CHECK(narrow_bitmap->bitmap_width() <= 40);
    CHECK(narrow_bitmap->bitmap_height() > 16);

    nk::TextNode wide(
        {0, 0}, {80, 60}, "This text should wrap across several lines", {0, 0, 0, 1}, {}, 80);
    renderer.begin_frame({64, 64}, 1.0F);
    renderer.render(wide);
    REQUIRE(renderer.text_commands_.size() == 1);
    const auto wide_bitmap = renderer.text_commands_[0].shaped_text;
    REQUIRE(wide_bitmap != nullptr);
    CHECK(wide_bitmap->bitmap_width() > narrow_bitmap->bitmap_width());
    CHECK(wide_bitmap->bitmap_width() <= 80);
}

TEST_CASE("Metal clears fractional damage with full pixel coverage", "[metal][render]") {
    MetalFixture fixture;
    nk::SnapshotContext snapshot;
    snapshot.add_color_rect({10.25F, 10.25F, 4.5F, 4.5F}, {1, 0, 0, 1});
    auto red = snapshot.take_root();
    frame(fixture.renderer, fixture.surface, *red);

    nk::SnapshotContext empty_snapshot;
    auto empty = empty_snapshot.take_root();
    const std::array<nk::Rect, 1> damage{{{10.25F, 10.25F, 4.5F, 4.5F}}};
    frame(fixture.renderer, fixture.surface, *empty, damage);
    check_pixel(fixture.renderer, fixture.renderer.scene_texture_, 10, 10, {255, 255, 255, 255});
}

TEST_CASE("Metal invalidates its preserved scene after software fallback", "[metal][render]") {
    MetalFixture fixture;
    auto red = solid({1, 0, 0, 1});
    frame(fixture.renderer, fixture.surface, *red);
    auto blue_fallback = fallback_root({0, 0, 1, 1});
    frame(fixture.renderer, fixture.surface, *blue_fallback);

    auto blue = solid({0, 0, 1, 1});
    const std::array<nk::Rect, 1> damage{{{40, 40, 10, 10}}};
    frame(fixture.renderer, fixture.surface, *blue, damage);
    check_pixel(fixture.renderer, fixture.renderer.scene_texture_, 5, 5, {0, 0, 255, 255});
}

TEST_CASE("Metal keeps fallback uploads independent until GPU completion", "[metal][render]") {
    MetalFixture fixture;
    id<MTLSharedEvent> event = [fixture.renderer.device_ newSharedEvent];
    id<MTLCommandBuffer> wait = [fixture.renderer.command_queue_ commandBuffer];
    [wait encodeWaitForEvent:event value:1];
    [wait commit];

    auto red = fallback_root({1, 0, 0, 1});
    frame(fixture.renderer, fixture.surface, *red);
    auto blue = fallback_root({0, 0, 1, 1});
    frame(fixture.renderer, fixture.surface, *blue);
    event.signaledValue = 1;

    REQUIRE(fixture.layer.captured.count == 2);
    id<CAMetalDrawable> first = [fixture.layer.captured objectAtIndex:0];
    id<CAMetalDrawable> second = [fixture.layer.captured objectAtIndex:1];
    check_pixel(fixture.renderer, first.texture, 5, 5, {255, 0, 0, 255});
    check_pixel(fixture.renderer, second.texture, 5, 5, {0, 0, 255, 255});
    [event release];
}

TEST_CASE("Metal drawable failure presents the current software frame", "[metal][render]") {
    TestSurface surface;
    surface.window.contentView.wantsLayer = YES;
    surface.window.contentView.layer = [NKMetalTestNilLayer layer];
    nk::MetalRenderer renderer;
    auto green = solid({0, 1, 0, 1});
    frame(renderer, surface, *green);

    REQUIRE(surface.cpu_presents == 1);
    REQUIRE(surface.cpu_width == 64);
    REQUIRE(surface.cpu_height == 64);
    REQUIRE(surface.cpu_pixels.size() == 64U * 64U * 4U);
    CHECK(surface.cpu_pixels[0] == 0);
    CHECK(surface.cpu_pixels[1] == 255);
    CHECK(surface.cpu_pixels[2] == 0);
    CHECK(surface.cpu_pixels[3] == 255);
}

TEST_CASE("Metal texture cache and scene resizing release device allocations", "[metal][memory]") {
    nk::MetalRenderer renderer;
    const auto before = renderer.device_.currentAllocatedSize;
    std::vector<uint32_t> image(256U * 256U, 0xFFFFFFFFU);
    nk::ImageCommand command;
    command.rect = {0, 0, 64, 64};
    command.pixel_data = image.data();
    command.src_width = 256;
    command.src_height = 256;
    for (uint32_t index = 0; index < 200; ++index) {
        renderer.begin_frame({64, 64}, 1.0F);
        image[0] = index;
        REQUIRE(renderer.upload_image_texture(command) != nil);
    }
    const auto full = renderer.device_.currentAllocatedSize;
    renderer.image_texture_cache_.clear();
    const auto cleared = renderer.device_.currentAllocatedSize;
    CHECK(full > before + 16U * 1024U * 1024U);
    CHECK(cleared + 16U * 1024U * 1024U < full);

    REQUIRE(renderer.ensure_scene_texture(64, 64));
    const auto first_scene = renderer.device_.currentAllocatedSize;
    REQUIRE(renderer.ensure_scene_texture(128, 128));
    const auto second_scene = renderer.device_.currentAllocatedSize;
    CHECK(second_scene >= first_scene);
    CHECK(second_scene - first_scene < 128U * 128U * 4U);
}
