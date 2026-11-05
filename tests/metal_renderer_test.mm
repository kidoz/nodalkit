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
// Keep the inspected implementation distinct from the class linked in nk_dep.
#define MetalRenderer MetalRendererUnderTest
#define create_metal_renderer create_metal_renderer_under_test
#include "../src/render/metal_renderer.mm"
#undef create_metal_renderer
#undef MetalRenderer
#undef private
#pragma clang diagnostic pop

@interface NKMetalTestCaptureLayer : CAMetalLayer
@property(nonatomic, retain) NSMutableArray* captured;
@end

@implementation NKMetalTestCaptureLayer

- (void)dealloc {
    [_captured release];
    [super dealloc];
}

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

@interface NKMetalTestTextureFaultDevice : NSObject
@property(nonatomic, retain) id<MTLDevice> realDevice;
@property(nonatomic, assign) BOOL failUpload;
@property(nonatomic, assign) NSUInteger failureCount;
@end

@implementation NKMetalTestTextureFaultDevice

- (void)dealloc {
    [_realDevice release];
    [super dealloc];
}

- (id)forwardingTargetForSelector:(SEL)selector {
    (void)selector;
    return _realDevice;
}

- (BOOL)supportsFamily:(MTLGPUFamily)family {
    return _failUpload ? NO : [_realDevice supportsFamily:family];
}

- (id<MTLTexture>)newTextureWithDescriptor:(MTLTextureDescriptor*)descriptor {
    if (!_failUpload && descriptor.pixelFormat == MTLPixelFormatRGBA8Unorm) {
        ++_failureCount;
        return nil;
    }
    return [_realDevice newTextureWithDescriptor:descriptor];
}

- (id<MTLBuffer>)newBufferWithLength:(NSUInteger)length options:(MTLResourceOptions)options {
    if (_failUpload) {
        ++_failureCount;
        return nil;
    }
    return [_realDevice newBufferWithLength:length options:options];
}

@end

namespace {

class GreenTextShaper final : public nk::TextShaper {
public:
    [[nodiscard]] nk::Size measure(std::string_view, const nk::FontDescriptor&) const override {
        return {64, 64};
    }

    [[nodiscard]] nk::ShapedText
    shape(std::string_view, const nk::FontDescriptor&, nk::Color) const override {
        nk::ShapedText shaped;
        std::vector<uint8_t> pixels(64U * 64U * 4U, 0);
        for (std::size_t index = 0; index < pixels.size(); index += 4) {
            pixels[index + 1] = 255;
            pixels[index + 3] = 255;
        }
        shaped.set_bitmap(std::move(pixels), 64, 64);
        return shaped;
    }
};

class TextureFaultScope {
public:
    TextureFaultScope(nk::MetalRendererUnderTest& renderer, bool fail_upload)
        : renderer_(renderer), original_device_(renderer.device_) {
        device = [[NKMetalTestTextureFaultDevice alloc] init];
        device.realDevice = original_device_;
        device.failUpload = fail_upload;
        renderer_.device_ = (id<MTLDevice>)device;
    }

    ~TextureFaultScope() {
        renderer_.device_ = original_device_;
        [device release];
    }

    TextureFaultScope(const TextureFaultScope&) = delete;
    TextureFaultScope& operator=(const TextureFaultScope&) = delete;

    NKMetalTestTextureFaultDevice* device = nil;

private:
    nk::MetalRendererUnderTest& renderer_;
    id<MTLDevice> original_device_ = nil;
};

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
    nk::MetalRendererUnderTest renderer;
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

void frame(nk::MetalRendererUnderTest& renderer,
           TestSurface& surface,
           const nk::RenderNode& root,
           std::span<const nk::Rect> damage = {}) {
    renderer.begin_frame({64, 64}, 1.0F);
    renderer.set_damage_regions(damage);
    renderer.render(root);
    renderer.end_frame();
    renderer.present(surface);
}

std::array<int, 4>
read_pixel(nk::MetalRendererUnderTest& renderer, id<MTLTexture> texture, int x, int y) {
    if (texture == nil) {
        return {-1, -1, -1, -1};
    }
    const NSUInteger stride = ((texture.width * 4 + 255) / 256) * 256;
    id<MTLBuffer> buffer = [renderer.device_ newBufferWithLength:stride * texture.height
                                                         options:MTLResourceStorageModeShared];
    REQUIRE(buffer != nil);
    id<MTLCommandBuffer> command_buffer = [renderer.command_queue_ commandBuffer];
    REQUIRE(command_buffer != nil);
    id<MTLBlitCommandEncoder> encoder = [command_buffer blitCommandEncoder];
    REQUIRE(encoder != nil);
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
    REQUIRE(command_buffer.status == MTLCommandBufferStatusCompleted);
    const NSUInteger texture_x = static_cast<NSUInteger>(x) * texture.width / 64;
    const NSUInteger texture_y = static_cast<NSUInteger>(y) * texture.height / 64;
    const auto* pixels =
        static_cast<const uint8_t*>(buffer.contents) + texture_y * stride + texture_x * 4;
    const bool bgra = texture.pixelFormat == MTLPixelFormatBGRA8Unorm;
    const std::array<int, 4> result{
        pixels[bgra ? 2 : 0], pixels[1], pixels[bgra ? 0 : 2], pixels[3]};
    [buffer release];
    return result;
}

void check_pixel(nk::MetalRendererUnderTest& renderer,
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
    nk::MetalRendererUnderTest renderer;
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
    nk::MetalRendererUnderTest renderer;
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

TEST_CASE("Metal recovery owns the rendered scene after render returns", "[metal][recovery]") {
    TestSurface surface;
    surface.window.contentView.wantsLayer = YES;
    surface.window.contentView.layer = [NKMetalTestNilLayer layer];
    auto renderer = nk::create_renderer(nk::RendererBackend::Metal);
    REQUIRE(renderer != nullptr);
    renderer->begin_frame({64, 64}, 1.0F);
    SECTION("primitive tree") {
        auto root = solid({0, 1, 0, 1});
        renderer->render(*root);
    }
    SECTION("owned image pixels") {
        auto pixels = std::make_shared<const std::vector<uint32_t>>(1, 0xFF00FF00U);
        nk::ImageNode image({0, 0, 64, 64}, pixels, 1, 1);
        renderer->render(image);
    }
    SECTION("borrowed image pixels") {
        std::vector<uint32_t> pixels(1, 0xFF00FF00U);
        nk::ImageNode image({0, 0, 64, 64}, pixels.data(), 1, 1);
        renderer->render(image);
    }
    renderer->end_frame();
    renderer->present(surface);
    REQUIRE(surface.cpu_pixels.size() == 64U * 64U * 4U);
    CHECK(surface.cpu_pixels[0] == 0);
    CHECK(surface.cpu_pixels[1] == 255);
    CHECK(surface.cpu_pixels[2] == 0);
    CHECK(surface.cpu_pixels[3] == 255);
}

TEST_CASE("Metal rejects empty and invalid framebuffers before presentation", "[metal][recovery]") {
    for (const auto viewport : std::array<nk::Size, 8>{{
             {0, 64},
             {64, 0},
             {-1, 64},
             {64, -1},
             {std::nanf(""), 64},
             {64, HUGE_VALF},
             {16384, 16384},
             {64, 64},
         }}) {
        CAPTURE(viewport.width, viewport.height);
        MetalFixture fixture;
        auto root = fallback_root({0, 1, 0, 1});
        // The last case overflows the scaled extent instead of the logical size.
        const float scale = viewport.width == 64 && viewport.height == 64 ? 1.0e30F : 1.0F;
        fixture.renderer.begin_frame(viewport, scale);
        fixture.renderer.render(*root);
        fixture.renderer.end_frame();
        fixture.renderer.present(fixture.surface);
        CHECK(fixture.surface.cpu_presents == 0);
        CHECK(fixture.layer.captured.count == 0);
        CHECK_FALSE(fixture.renderer.scene_initialized_);

        auto green = solid({0, 1, 0, 1});
        frame(fixture.renderer, fixture.surface, *green);
        check_pixel(fixture.renderer, fixture.renderer.scene_texture_, 5, 5, {0, 255, 0, 255});
    }
}

TEST_CASE("Metal recovery replays a mixed scene after its tree is released", "[metal][recovery]") {
    TestSurface surface;
    surface.window.contentView.wantsLayer = YES;
    surface.window.contentView.layer = [NKMetalTestNilLayer layer];
    auto renderer = nk::create_renderer(nk::RendererBackend::Metal);
    REQUIRE(renderer != nullptr);
    auto shaper = nk::TextShaper::create();
    REQUIRE(shaper != nullptr);
    renderer->set_text_shaper(shaper.get());
    renderer->begin_frame({64, 64}, 2.0F);
    std::vector<uint8_t> expected;
    {
        nk::SnapshotContext snapshot(shaper.get());
        snapshot.push_container({0, 0, 64, 64});
        snapshot.add_color_rect({0, 0, 64, 64}, {0.2F, 0.3F, 0.4F, 1});
        snapshot.push_rounded_clip({2, 2, 60, 60}, 8);
        snapshot.add_rounded_rect({2, 2, 60, 60}, {1, 0, 0, 0.5F}, 7);
        snapshot.add_border({4, 4, 56, 56}, {0, 0, 1, 1}, 2, 5);
        snapshot.add_line({5, 5}, {55, 45}, {1, 1, 0, 1}, 2);
        snapshot.add_wrapped_text({8, 8}, "recovery wraps text", {0, 0, 0, 1}, {}, 30);
        std::vector<uint32_t> image(4, 0xFF00FF00U);
        snapshot.add_image({40, 40, 12, 12}, image.data(), 2, 2);
        snapshot.pop_container();
        snapshot.pop_container();
        auto root = snapshot.take_root();
        nk::SoftwareRenderer reference;
        reference.set_text_shaper(shaper.get());
        reference.begin_frame({64, 64}, 2.0F);
        reference.render(*root);
        reference.end_frame();
        expected.assign(reference.pixel_data(), reference.pixel_data() + 128U * 128U * 4U);
        renderer->render(*root);
    }
    renderer->end_frame();
    renderer->present(surface);
    CHECK(surface.cpu_pixels == expected);
}

TEST_CASE("Metal owns deferred image pixels without changing texture cache identity",
          "[metal][recovery]") {
    MetalFixture fixture;
    std::vector<uint32_t> pixels(1, 0xFF00FF00U);
    for (int iteration = 0; iteration < 2; ++iteration) {
        fixture.renderer.begin_frame({64, 64}, 1.0F);
        {
            nk::ImageNode image({0, 0, 64, 64}, pixels.data(), 1, 1);
            fixture.renderer.render(image);
        }
        pixels[0] = 0xFFFF0000U;
        fixture.renderer.end_frame();
        fixture.renderer.present(fixture.surface);
        check_pixel(fixture.renderer, fixture.renderer.scene_texture_, 5, 5, {0, 255, 0, 255});
        CHECK(fixture.renderer.last_hotspot_counters().image_texture_upload_count ==
              (iteration == 0 ? 1 : 0));
        pixels[0] = 0xFF00FF00U;
    }
}

TEST_CASE("Metal texture failures recover the full frame in software", "[metal][recovery]") {
    bool text = false;
    bool upload = false;
    SECTION("image allocation") {}
    SECTION("text allocation") {
        text = true;
    }
    SECTION("image upload") {
        upload = true;
    }
    SECTION("text upload") {
        text = true;
        upload = true;
    }

    for (const bool partial : {false, true}) {
        CAPTURE(partial);
        MetalFixture fixture;
        GreenTextShaper shaper;
        fixture.renderer.set_text_shaper(&shaper);
        auto blue = solid({0, 0, 1, 1});
        frame(fixture.renderer, fixture.surface, *blue);

        nk::SnapshotContext snapshot;
        snapshot.add_color_rect({0, 0, 64, 64}, {1, 0, 0, 1});
        const uint32_t green_pixel = 0xFF00FF00U;
        if (text) {
            snapshot.add_text({0, 0}, "green", {0, 1, 0, 1});
        } else {
            snapshot.add_image({0, 0, 64, 64}, &green_pixel, 1, 1);
        }
        auto root = snapshot.take_root();
        {
            TextureFaultScope fault(fixture.renderer, upload);
            const std::array<nk::Rect, 1> damage{{{40, 40, 10, 10}}};
            frame(fixture.renderer,
                  fixture.surface,
                  *root,
                  partial ? std::span<const nk::Rect>(damage) : std::span<const nk::Rect>{});
            REQUIRE(fault.device.failureCount > 0);
        }
        REQUIRE(fixture.surface.cpu_presents == 1);
        REQUIRE(fixture.surface.cpu_pixels.size() == 64U * 64U * 4U);
        CHECK(fixture.surface.cpu_pixels[0] == 0);
        CHECK(fixture.surface.cpu_pixels[1] == 255);
        CHECK(fixture.surface.cpu_pixels[2] == 0);
        CHECK(fixture.surface.cpu_pixels[3] == 255);
        CHECK_FALSE(fixture.renderer.scene_initialized_);
        CHECK(fixture.renderer.last_hotspot_counters().gpu_present_path ==
              nk::GpuPresentPath::SoftwareUpload);

        auto green = solid({0, 1, 0, 1});
        const std::array<nk::Rect, 1> damage{{{40, 40, 10, 10}}};
        frame(fixture.renderer, fixture.surface, *green, damage);
        check_pixel(fixture.renderer, fixture.renderer.scene_texture_, 5, 5, {0, 255, 0, 255});
    }
}

TEST_CASE("Metal uploads padded rows into private textures", "[metal][upload]") {
    nk::MetalRendererUnderTest renderer;
    // Exercise the Intel/AMD upload path even when the test host supports shared textures.
    auto* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:3
                                                          height:2
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageShaderRead;
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> texture = [renderer.device_ newTextureWithDescriptor:descriptor];
    REQUIRE(texture != nil);
    const std::array<uint8_t, 24> pixels{
        11,  22,  33,  255, 44,  55,  66,  255, 77,  88, 99, 255,
        101, 102, 103, 255, 104, 105, 106, 255, 199, 66, 88, 255,
    };
    REQUIRE(nk::upload_texture_region(renderer.device_,
                                      renderer.command_queue_,
                                      texture,
                                      MTLRegionMake2D(0, 0, 3, 2),
                                      pixels.data(),
                                      12));
    check_pixel(renderer, texture, 0, 0, {11, 22, 33, 255});
    check_pixel(renderer, texture, 63, 63, {199, 66, 88, 255});
    [texture release];
}

TEST_CASE("Metal renderer destruction releases scene allocations", "[metal][memory]") {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    REQUIRE(device != nil);
    const auto before = device.currentAllocatedSize;
    for (int iteration = 0; iteration < 3; ++iteration) {
        @autoreleasepool {
            nk::MetalRendererUnderTest renderer;
            REQUIRE(renderer.ensure_scene_texture(1024, 1024));
            CHECK(device.currentAllocatedSize >= before + 4U * 1024U * 1024U);
        }
    }
    CHECK(device.currentAllocatedSize < before + 1024U * 1024U);
    [device release];
}

TEST_CASE("Metal texture cache and scene resizing release device allocations", "[metal][memory]") {
    nk::MetalRendererUnderTest renderer;
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
