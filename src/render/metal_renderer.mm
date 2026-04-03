#include <nk/render/renderer.h>

#if defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <nk/foundation/logging.h>
#include <nk/platform/platform_backend.h>
#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <nk/text/shaped_text.h>
#include <nk/text/text_shaper.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace nk {

namespace {

struct MetalVertex {
    float position[2];
    float tex_coord[2];
};

struct PrimitiveVertex {
    float unit_position[2];
};

struct ImageUniforms {
    float rect[4];
    uint32_t clip_count = 0;
    uint32_t clip_padding[3]{};
    float clip_rects[16]{};
    float clip_radii[4]{};
    float viewport_size[2];
};

static_assert(offsetof(ImageUniforms, clip_rects) == 32);
static_assert(offsetof(ImageUniforms, viewport_size) == 112);

struct PrimitiveUniforms {
    float rect[4];
    float color[4];
    float radius = 0.0F;
    float thickness = 0.0F;
    uint32_t kind = 0;
    uint32_t clip_count = 0;
    float clip_rects[16]{};
    float clip_radii[4]{};
    float viewport_size[2];
};

struct ClipRegion {
    Rect rect{};
    float radius = 0.0F;
};

struct PrimitiveCommand {
    Rect rect{};
    Color color{};
    float radius = 0.0F;
    float thickness = 0.0F;
    uint32_t kind = 0;
    std::array<ClipRegion, 4> clips{};
    uint32_t clip_count = 0;
};

struct ImageCommand {
    Rect rect{};
    const uint32_t* pixel_data = nullptr;
    int src_width = 0;
    int src_height = 0;
    ScaleMode scale_mode = ScaleMode::NearestNeighbor;
    std::array<ClipRegion, 4> clips{};
    uint32_t clip_count = 0;
};

struct TextCommand {
    Rect rect{};
    std::shared_ptr<ShapedText> shaped_text;
    std::array<ClipRegion, 4> clips{};
    uint32_t clip_count = 0;
};

enum class DrawCommandKind : uint8_t {
    Primitive,
    Image,
    Text,
};

struct DrawCommand {
    DrawCommandKind kind = DrawCommandKind::Primitive;
    std::size_t command_index = 0;
};

struct TextKey {
    std::string text;
    std::string family;
    float size = 0.0F;
    int weight = 0;
    int style = 0;
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 0.0F;
    float scale = 1.0F;

    bool operator==(const TextKey& other) const {
        return text == other.text && family == other.family && size == other.size &&
               weight == other.weight && style == other.style && r == other.r && g == other.g &&
               b == other.b && a == other.a && scale == other.scale;
    }
};

struct TextKeyHash {
    std::size_t operator()(const TextKey& key) const {
        std::size_t hash = std::hash<std::string>{}(key.text);
        hash ^= std::hash<std::string>{}(key.family) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.size) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.weight) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.style) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.r) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.g) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.b) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.a) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.scale) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

struct ImageTextureCacheKey {
    const uint32_t* pixel_data = nullptr;
    int width = 0;
    int height = 0;
    std::size_t content_hash = 0;

    bool operator==(const ImageTextureCacheKey& other) const {
        return pixel_data == other.pixel_data && width == other.width && height == other.height &&
               content_hash == other.content_hash;
    }
};

struct TextTextureCacheKey {
    const ShapedText* shaped_text = nullptr;
    const uint8_t* bitmap_data = nullptr;
    int width = 0;
    int height = 0;

    bool operator==(const TextTextureCacheKey& other) const {
        return shaped_text == other.shaped_text && bitmap_data == other.bitmap_data &&
               width == other.width && height == other.height;
    }
};

struct TextureCacheEntry {
    id<MTLTexture> texture = nil;
    uint64_t last_used_frame = 0;
};

struct ImageTextureCacheKeyHash {
    std::size_t operator()(const ImageTextureCacheKey& key) const {
        std::size_t hash = std::hash<const uint32_t*>{}(key.pixel_data);
        hash ^= std::hash<int>{}(key.width) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.height) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<std::size_t>{}(key.content_hash) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

struct TextTextureCacheKeyHash {
    std::size_t operator()(const TextTextureCacheKey& key) const {
        std::size_t hash = std::hash<const ShapedText*>{}(key.shaped_text);
        hash ^=
            std::hash<const uint8_t*>{}(key.bitmap_data) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.width) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<int>{}(key.height) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

constexpr std::array<MetalVertex, 4> kFullscreenQuad = {{
    {{-1.0F, -1.0F}, {0.0F, 1.0F}},
    {{1.0F, -1.0F}, {1.0F, 1.0F}},
    {{-1.0F, 1.0F}, {0.0F, 0.0F}},
    {{1.0F, 1.0F}, {1.0F, 0.0F}},
}};

constexpr std::array<PrimitiveVertex, 4> kUnitQuad = {{
    {{0.0F, 0.0F}},
    {{1.0F, 0.0F}},
    {{0.0F, 1.0F}},
    {{1.0F, 1.0F}},
}};

float normalize_scale_factor(float scale_factor) {
    return std::isfinite(scale_factor) && scale_factor > 0.0F ? scale_factor : 1.0F;
}

std::size_t scaled_pixel_area(Rect rect, float scale_factor) {
    if (rect.width <= 0.0F || rect.height <= 0.0F) {
        return 0;
    }
    const auto width = std::max(
        1, static_cast<int>(std::lround(rect.width * normalize_scale_factor(scale_factor))));
    const auto height = std::max(
        1, static_cast<int>(std::lround(rect.height * normalize_scale_factor(scale_factor))));
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

Rect scale_rect(Rect rect, float scale_factor) {
    return {
        rect.x * scale_factor,
        rect.y * scale_factor,
        rect.width * scale_factor,
        rect.height * scale_factor,
    };
}

float effective_corner_radius(Rect rect, float requested_radius) {
    return std::clamp(requested_radius, 0.0F, std::min(rect.width, rect.height) * 0.5F);
}

bool rects_overlap_or_touch(Rect lhs, Rect rhs) {
    return lhs.x <= rhs.right() && rhs.x <= lhs.right() && lhs.y <= rhs.bottom() &&
           rhs.y <= lhs.bottom();
}

bool rects_intersect(Rect lhs, Rect rhs) {
    return lhs.x < rhs.right() && rhs.x < lhs.right() && lhs.y < rhs.bottom() &&
           rhs.y < lhs.bottom();
}

Rect union_rect(Rect lhs, Rect rhs) {
    const float x0 = std::min(lhs.x, rhs.x);
    const float y0 = std::min(lhs.y, rhs.y);
    const float x1 = std::max(lhs.right(), rhs.right());
    const float y1 = std::max(lhs.bottom(), rhs.bottom());
    return {x0, y0, std::max(0.0F, x1 - x0), std::max(0.0F, y1 - y0)};
}

constexpr uint64_t kTextureCacheMaxAgeFrames = 120;
constexpr std::size_t kImageTextureCacheMaxEntries = 128;
constexpr std::size_t kTextTextureCacheMaxEntries = 256;

std::size_t hash_bytes(const uint8_t* data, std::size_t size) {
    std::size_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::size_t>(data[index]);
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename Uniforms, typename ClipArray>
void populate_clip_uniforms(Uniforms& uniforms,
                            const ClipArray& clips,
                            uint32_t clip_count,
                            float scale_factor) {
    uniforms.clip_count = clip_count;
    for (uint32_t index = 0; index < clip_count; ++index) {
        const Rect scaled_clip = scale_rect(clips[index].rect, scale_factor);
        uniforms.clip_rects[index * 4 + 0] = scaled_clip.x;
        uniforms.clip_rects[index * 4 + 1] = scaled_clip.y;
        uniforms.clip_rects[index * 4 + 2] = scaled_clip.width;
        uniforms.clip_rects[index * 4 + 3] = scaled_clip.height;
        uniforms.clip_radii[index] = clips[index].radius * scale_factor;
    }
}

id<MTLLibrary> build_shader_library(id<MTLDevice> device) {
    static NSString* source = @R"(
        #include <metal_stdlib>
        using namespace metal;

        struct VertexIn {
            float2 position;
            float2 tex_coord;
        };

        struct VertexOut {
            float4 position [[position]];
            float2 tex_coord;
        };

        struct PrimitiveVertexIn {
            float2 unit_position;
        };

        vertex VertexOut nk_metal_vertex(const device VertexIn* vertices [[buffer(0)]],
                                         uint vertex_id [[vertex_id]]) {
            VertexOut out;
            out.position = float4(vertices[vertex_id].position, 0.0, 1.0);
            out.tex_coord = vertices[vertex_id].tex_coord;
            return out;
        }

        fragment float4 nk_metal_fragment(VertexOut in [[stage_in]],
                                          texture2d<float> color_texture [[texture(0)]],
                                          sampler color_sampler [[sampler(0)]]) {
            return color_texture.sample(color_sampler, in.tex_coord);
        }

        struct ImageUniforms {
            float4 rect;
            uint clip_count;
            float4 clip_rects[4];
            float clip_radii[4];
            float2 viewport_size;
        };

        struct ImageVertexOut {
            float4 position [[position]];
            float2 tex_coord;
            float2 pixel_position;
        };

        vertex ImageVertexOut nk_metal_image_vertex(
            const device PrimitiveVertexIn* vertices [[buffer(0)]],
            constant ImageUniforms& uniforms [[buffer(1)]],
            uint vertex_id [[vertex_id]]) {
            ImageVertexOut out;
            float2 unit = vertices[vertex_id].unit_position;
            float2 pixel = uniforms.rect.xy + uniforms.rect.zw * unit;
            float2 ndc;
            ndc.x = (pixel.x / uniforms.viewport_size.x) * 2.0 - 1.0;
            ndc.y = 1.0 - ((pixel.y / uniforms.viewport_size.y) * 2.0);
            out.position = float4(ndc, 0.0, 1.0);
            out.tex_coord = unit;
            out.pixel_position = pixel;
            return out;
        }

        struct PrimitiveUniforms {
            float4 rect;
            float4 color;
            float radius;
            float thickness;
            uint kind;
            uint clip_count;
            float4 clip_rects[4];
            float clip_radii[4];
            float2 viewport_size;
        };

        struct PrimitiveVertexOut {
            float4 position [[position]];
            float2 pixel_position;
            float4 rect;
            float4 color;
            float radius;
            float thickness;
            uint kind;
        };

        float rounded_rect_sd(float2 pixel, float4 rect, float radius) {
            float clamped_radius = clamp(radius, 0.0, min(rect.z, rect.w) * 0.5);
            float2 center = rect.xy + rect.zw * 0.5;
            float2 local = abs(pixel - center);
            float2 half_size = rect.zw * 0.5;
            float2 inner = max(half_size - clamped_radius, float2(0.0));
            float2 q = local - inner;
            float outside = length(max(q, float2(0.0)));
            float inside = min(max(q.x, q.y), 0.0);
            return outside + inside - clamped_radius;
        }

        vertex PrimitiveVertexOut nk_metal_primitive_vertex(
            const device PrimitiveVertexIn* vertices [[buffer(0)]],
            constant PrimitiveUniforms& uniforms [[buffer(1)]],
            uint vertex_id [[vertex_id]]) {
            PrimitiveVertexOut out;
            float2 unit = vertices[vertex_id].unit_position;
            float2 pixel = uniforms.rect.xy + uniforms.rect.zw * unit;
            float2 ndc;
            ndc.x = (pixel.x / uniforms.viewport_size.x) * 2.0 - 1.0;
            ndc.y = 1.0 - ((pixel.y / uniforms.viewport_size.y) * 2.0);
            out.position = float4(ndc, 0.0, 1.0);
            out.pixel_position = pixel;
            out.rect = uniforms.rect;
            out.color = uniforms.color;
            out.radius = uniforms.radius;
            out.thickness = uniforms.thickness;
            out.kind = uniforms.kind;
            return out;
        }

        fragment float4 nk_metal_primitive_fragment(PrimitiveVertexOut in [[stage_in]],
                                                    constant PrimitiveUniforms& uniforms [[buffer(1)]]) {
            float coverage = saturate(0.5 - rounded_rect_sd(in.pixel_position, in.rect, in.radius));
            for (uint clip_index = 0; clip_index < uniforms.clip_count; ++clip_index) {
                float clip_coverage = saturate(
                    0.5 - rounded_rect_sd(in.pixel_position,
                                          uniforms.clip_rects[clip_index],
                                          uniforms.clip_radii[clip_index]));
                coverage *= clip_coverage;
            }
            if (in.kind == 1) {
                float inner_radius = max(0.0, in.radius - in.thickness);
                float4 inner_rect = float4(in.rect.x + in.thickness,
                                           in.rect.y + in.thickness,
                                           max(0.0, in.rect.z - (in.thickness * 2.0)),
                                           max(0.0, in.rect.w - (in.thickness * 2.0)));
                float inner_coverage =
                    saturate(0.5 - rounded_rect_sd(in.pixel_position, inner_rect, inner_radius));
                coverage = saturate(coverage - inner_coverage);
            }
            return float4(in.color.rgb, in.color.a * coverage);
        }

        fragment float4 nk_metal_image_fragment(ImageVertexOut in [[stage_in]],
                                                constant ImageUniforms& uniforms [[buffer(1)]],
                                                texture2d<float> color_texture [[texture(0)]],
                                                sampler color_sampler [[sampler(0)]]) {
            float coverage = 1.0;
            for (uint clip_index = 0; clip_index < uniforms.clip_count; ++clip_index) {
                float clip_coverage = saturate(
                    0.5 - rounded_rect_sd(in.pixel_position,
                                          uniforms.clip_rects[clip_index],
                                          uniforms.clip_radii[clip_index]));
                coverage *= clip_coverage;
            }
            float4 sample = color_texture.sample(color_sampler, in.tex_coord);
            return float4(sample.rgb, sample.a * coverage);
        }
    )";

    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (library == nil) {
        if (error != nil) {
            std::string message = "Failed to compile Metal shaders: ";
            message += error.localizedDescription.UTF8String;
            NK_LOG_ERROR("MetalRenderer", message.c_str());
        }
        return nil;
    }
    return library;
}

} // namespace

class MetalRenderer final : public Renderer {
public:
    MetalRenderer()
        : device_(MTLCreateSystemDefaultDevice()), software_(std::make_unique<SoftwareRenderer>()) {
        if (device_ == nil) {
            return;
        }

        command_queue_ = [device_ newCommandQueue];
        library_ = build_shader_library(device_);
        if (command_queue_ == nil || library_ == nil) {
            return;
        }

        id<MTLFunction> texture_vertex_function = [library_ newFunctionWithName:@"nk_metal_vertex"];
        id<MTLFunction> texture_fragment_function =
            [library_ newFunctionWithName:@"nk_metal_fragment"];
        id<MTLFunction> image_vertex_function =
            [library_ newFunctionWithName:@"nk_metal_image_vertex"];
        id<MTLFunction> image_fragment_function =
            [library_ newFunctionWithName:@"nk_metal_image_fragment"];
        id<MTLFunction> primitive_vertex_function =
            [library_ newFunctionWithName:@"nk_metal_primitive_vertex"];
        id<MTLFunction> primitive_fragment_function =
            [library_ newFunctionWithName:@"nk_metal_primitive_fragment"];
        if (texture_vertex_function == nil || texture_fragment_function == nil ||
            image_vertex_function == nil || image_fragment_function == nil ||
            primitive_vertex_function == nil || primitive_fragment_function == nil) {
            return;
        }

        MTLRenderPipelineDescriptor* texture_descriptor =
            [[MTLRenderPipelineDescriptor alloc] init];
        texture_descriptor.vertexFunction = texture_vertex_function;
        texture_descriptor.fragmentFunction = texture_fragment_function;
        texture_descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        NSError* pipeline_error = nil;
        texture_pipeline_ = [device_ newRenderPipelineStateWithDescriptor:texture_descriptor
                                                                    error:&pipeline_error];
        if (texture_pipeline_ == nil && pipeline_error != nil) {
            std::string message = "Failed to create Metal pipeline: ";
            message += pipeline_error.localizedDescription.UTF8String;
            NK_LOG_ERROR("MetalRenderer", message.c_str());
            return;
        }

        MTLRenderPipelineDescriptor* image_descriptor = [[MTLRenderPipelineDescriptor alloc] init];
        image_descriptor.vertexFunction = image_vertex_function;
        image_descriptor.fragmentFunction = image_fragment_function;
        image_descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        image_descriptor.colorAttachments[0].blendingEnabled = YES;
        image_descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        image_descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        image_descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        image_descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        image_descriptor.colorAttachments[0].destinationRGBBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        image_descriptor.colorAttachments[0].destinationAlphaBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;

        pipeline_error = nil;
        image_pipeline_ = [device_ newRenderPipelineStateWithDescriptor:image_descriptor
                                                                  error:&pipeline_error];
        if (image_pipeline_ == nil && pipeline_error != nil) {
            std::string message = "Failed to create Metal image pipeline: ";
            message += pipeline_error.localizedDescription.UTF8String;
            NK_LOG_ERROR("MetalRenderer", message.c_str());
            return;
        }

        MTLRenderPipelineDescriptor* primitive_descriptor =
            [[MTLRenderPipelineDescriptor alloc] init];
        primitive_descriptor.vertexFunction = primitive_vertex_function;
        primitive_descriptor.fragmentFunction = primitive_fragment_function;
        primitive_descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        primitive_descriptor.colorAttachments[0].blendingEnabled = YES;
        primitive_descriptor.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        primitive_descriptor.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        primitive_descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        primitive_descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
        primitive_descriptor.colorAttachments[0].destinationRGBBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;
        primitive_descriptor.colorAttachments[0].destinationAlphaBlendFactor =
            MTLBlendFactorOneMinusSourceAlpha;

        pipeline_error = nil;
        primitive_pipeline_ = [device_ newRenderPipelineStateWithDescriptor:primitive_descriptor
                                                                      error:&pipeline_error];
        if (primitive_pipeline_ == nil && pipeline_error != nil) {
            std::string message = "Failed to create Metal primitive pipeline: ";
            message += pipeline_error.localizedDescription.UTF8String;
            NK_LOG_ERROR("MetalRenderer", message.c_str());
            return;
        }

        vertex_buffer_ = [device_
            newBufferWithBytes:kFullscreenQuad.data()
                        length:static_cast<NSUInteger>(sizeof(MetalVertex) * kFullscreenQuad.size())
                       options:MTLResourceStorageModeShared];
        primitive_vertex_buffer_ = [device_
            newBufferWithBytes:kUnitQuad.data()
                        length:static_cast<NSUInteger>(sizeof(PrimitiveVertex) * kUnitQuad.size())
                       options:MTLResourceStorageModeShared];

        MTLSamplerDescriptor* sampler_descriptor = [[MTLSamplerDescriptor alloc] init];
        sampler_descriptor.minFilter = MTLSamplerMinMagFilterNearest;
        sampler_descriptor.magFilter = MTLSamplerMinMagFilterNearest;
        sampler_descriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler_state_ = [device_ newSamplerStateWithDescriptor:sampler_descriptor];

        sampler_descriptor.minFilter = MTLSamplerMinMagFilterLinear;
        sampler_descriptor.magFilter = MTLSamplerMinMagFilterLinear;
        linear_sampler_state_ = [device_ newSamplerStateWithDescriptor:sampler_descriptor];
    }

    ~MetalRenderer() override = default;

    [[nodiscard]] RendererBackend backend() const override { return RendererBackend::Metal; }

    bool attach_surface(NativeSurface& surface) override {
        if (device_ == nil || texture_pipeline_ == nil || image_pipeline_ == nil ||
            primitive_pipeline_ == nil || command_queue_ == nil) {
            return false;
        }

        NSWindow* window = (__bridge NSWindow*)surface.native_handle();
        if (window == nil) {
            return false;
        }

        NSView* view = window.contentView;
        if (view == nil) {
            return false;
        }

        [view setWantsLayer:YES];
        CAMetalLayer* metal_layer = nil;
        if ([view.layer isKindOfClass:[CAMetalLayer class]]) {
            metal_layer = static_cast<CAMetalLayer*>(view.layer);
        } else {
            metal_layer = [CAMetalLayer layer];
            metal_layer.device = device_;
            metal_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            metal_layer.framebufferOnly = YES;
            metal_layer.contentsGravity = kCAGravityResizeAspect;
            view.layer = metal_layer;
        }

        layer_ = metal_layer;
        if (layer_ == nil) {
            return false;
        }

        layer_.device = device_;
        layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer_.opaque = YES;
        const CGSize bounds = view.bounds.size;
        const CGFloat scale = window.backingScaleFactor > 0.0 ? window.backingScaleFactor : 1.0;
        layer_.contentsScale = scale;
        layer_.drawableSize = CGSizeMake(bounds.width * scale, bounds.height * scale);
        layer_.frame = view.bounds;
        return true;
    }

    void set_text_shaper(TextShaper* shaper) override {
        text_shaper_ = shaper;
        software_->set_text_shaper(shaper);
    }

    void set_damage_regions(std::span<const Rect> regions) override {
        damage_regions_.clear();
        if (logical_viewport_.width <= 0.0F || logical_viewport_.height <= 0.0F ||
            regions.empty()) {
            last_hotspot_counters_.damage_region_count = 0;
            full_redraw_ = true;
            return;
        }

        damage_regions_.reserve(regions.size());
        const Rect viewport = {0.0F,
                               0.0F,
                               logical_viewport_.width * scale_factor_,
                               logical_viewport_.height * scale_factor_};
        for (const auto& region : regions) {
            Rect scaled = scale_rect(region, scale_factor_);
            const float x0 = std::max(viewport.x, scaled.x);
            const float y0 = std::max(viewport.y, scaled.y);
            const float x1 = std::min(viewport.right(), scaled.right());
            const float y1 = std::min(viewport.bottom(), scaled.bottom());
            scaled = {x0, y0, std::max(0.0F, x1 - x0), std::max(0.0F, y1 - y0)};
            if (scaled.width <= 0.0F || scaled.height <= 0.0F) {
                continue;
            }

            bool merged_existing = false;
            for (auto& existing : damage_regions_) {
                if (rects_overlap_or_touch(existing, scaled)) {
                    existing = union_rect(existing, scaled);
                    merged_existing = true;
                    break;
                }
            }

            if (!merged_existing) {
                damage_regions_.push_back(scaled);
            }
        }

        for (std::size_t outer = 0; outer < damage_regions_.size(); ++outer) {
            for (std::size_t inner = outer + 1; inner < damage_regions_.size();) {
                if (rects_overlap_or_touch(damage_regions_[outer], damage_regions_[inner])) {
                    damage_regions_[outer] =
                        union_rect(damage_regions_[outer], damage_regions_[inner]);
                    damage_regions_.erase(damage_regions_.begin() +
                                          static_cast<std::ptrdiff_t>(inner));
                } else {
                    ++inner;
                }
            }
        }

        last_hotspot_counters_.damage_region_count = damage_regions_.size();
        full_redraw_ = damage_regions_.empty();
    }

    void begin_frame(Size viewport, float scale_factor) override {
        logical_viewport_ = viewport;
        scale_factor_ = normalize_scale_factor(scale_factor);
        ++frame_serial_;
        draw_commands_.clear();
        primitive_commands_.clear();
        image_commands_.clear();
        text_commands_.clear();
        active_clips_.clear();
        trim_texture_cache(image_texture_cache_, kImageTextureCacheMaxEntries);
        trim_texture_cache(text_texture_cache_, kTextTextureCacheMaxEntries);
        needs_software_fallback_ = false;
        damage_regions_.clear();
        full_redraw_ = true;
        last_hotspot_counters_ = {};
    }

    void render(const RenderNode& root) override {
        const auto preserved_damage_region_count = last_hotspot_counters_.damage_region_count;
        last_hotspot_counters_ = {};
        last_hotspot_counters_.damage_region_count = preserved_damage_region_count;
        draw_commands_.clear();
        primitive_commands_.clear();
        image_commands_.clear();
        text_commands_.clear();
        active_clips_.clear();
        if (!collect_draw_commands(root)) {
            needs_software_fallback_ = true;
            software_->begin_frame(logical_viewport_, scale_factor_);
            software_->render(root);
            last_hotspot_counters_ = software_->last_hotspot_counters();
        }
    }

    void end_frame() override {
        if (needs_software_fallback_) {
            software_->end_frame();
        }
    }

    void present(NativeSurface& surface) override {
        if (!attach_surface(surface) || layer_ == nil) {
            software_->present(surface);
            return;
        }

        const int width =
            std::max(1, static_cast<int>(std::lround(logical_viewport_.width * scale_factor_)));
        const int height =
            std::max(1, static_cast<int>(std::lround(logical_viewport_.height * scale_factor_)));
        if (width <= 0 || height <= 0) {
            return;
        }

        id<CAMetalDrawable> drawable = [layer_ nextDrawable];
        if (drawable == nil) {
            software_->present(surface);
            return;
        }

        id<MTLCommandBuffer> command_buffer = [command_queue_ commandBuffer];
        if (command_buffer == nil) {
            software_->present(surface);
            return;
        }

        if (needs_software_fallback_) {
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = drawable.texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(1.0, 1.0, 1.0, 1.0);
            id<MTLRenderCommandEncoder> encoder =
                [command_buffer renderCommandEncoderWithDescriptor:pass];
            if (encoder == nil) {
                software_->present(surface);
                return;
            }

            if (staging_texture_ == nil || static_cast<int>(staging_texture_.width) != width ||
                static_cast<int>(staging_texture_.height) != height) {
                MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
                    texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                 width:static_cast<NSUInteger>(width)
                                                height:static_cast<NSUInteger>(height)
                                             mipmapped:NO];
                descriptor.usage = MTLTextureUsageShaderRead;
                descriptor.storageMode = MTLStorageModeShared;
                staging_texture_ = [device_ newTextureWithDescriptor:descriptor];
            }

            if (staging_texture_ == nil) {
                [encoder endEncoding];
                software_->present(surface);
                return;
            }

            const MTLRegion region = MTLRegionMake2D(
                0, 0, static_cast<NSUInteger>(width), static_cast<NSUInteger>(height));
            [staging_texture_ replaceRegion:region
                                mipmapLevel:0
                                  withBytes:software_->pixel_data()
                                bytesPerRow:static_cast<NSUInteger>(width * 4)];

            [encoder setRenderPipelineState:texture_pipeline_];
            [encoder setVertexBuffer:vertex_buffer_ offset:0 atIndex:0];
            [encoder setFragmentTexture:staging_texture_ atIndex:0];
            if (sampler_state_ != nil) {
                [encoder setFragmentSamplerState:sampler_state_ atIndex:0];
            }
            [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            ++last_hotspot_counters_.gpu_draw_call_count;
            last_hotspot_counters_.gpu_present_path = GpuPresentPath::SoftwareUpload;
            last_hotspot_counters_.gpu_present_tradeoff = GpuPresentTradeoff::None;
            last_hotspot_counters_.gpu_present_region_count = 0;
            [encoder endEncoding];
            [command_buffer presentDrawable:drawable];
            [command_buffer commit];
            return;
        }

        if (!ensure_scene_texture(width, height)) {
            software_->present(surface);
            return;
        }

        const bool partial_redraw = scene_initialized_ && !full_redraw_ && !damage_regions_.empty();
        MTLRenderPassDescriptor* scene_pass = [MTLRenderPassDescriptor renderPassDescriptor];
        scene_pass.colorAttachments[0].texture = scene_texture_;
        scene_pass.colorAttachments[0].loadAction =
            partial_redraw ? MTLLoadActionLoad : MTLLoadActionClear;
        scene_pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        scene_pass.colorAttachments[0].clearColor = MTLClearColorMake(1.0, 1.0, 1.0, 1.0);

        id<MTLRenderCommandEncoder> scene_encoder =
            [command_buffer renderCommandEncoderWithDescriptor:scene_pass];
        if (scene_encoder == nil) {
            software_->present(surface);
            return;
        }

        id<MTLRenderPipelineState> active_pipeline = nil;
        if (partial_redraw) {
            std::size_t damage_pixel_count = 0;
            for (const auto& damage_region : damage_regions_) {
                const auto scissor = scissor_rect(damage_region, width, height);
                if (scissor.width == 0 || scissor.height == 0) {
                    continue;
                }
                damage_pixel_count += scissor.width * scissor.height;
                [scene_encoder setScissorRect:scissor];
                encode_clear_rect(scene_encoder, active_pipeline, damage_region, width, height);
                for (const auto& draw_command : draw_commands_) {
                    if (!draw_command_intersects_damage(draw_command, damage_region)) {
                        continue;
                    }
                    encode_draw_command(
                        scene_encoder, active_pipeline, draw_command, width, height);
                }
            }
            last_hotspot_counters_.gpu_present_region_count = damage_regions_.size();
            last_hotspot_counters_.gpu_present_path = GpuPresentPath::PartialRedrawCopy;
            last_hotspot_counters_.gpu_present_tradeoff = GpuPresentTradeoff::DrawFavored;
            last_hotspot_counters_.gpu_estimated_draw_pixel_count =
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) +
                damage_pixel_count;
        } else {
            for (const auto& draw_command : draw_commands_) {
                encode_draw_command(scene_encoder, active_pipeline, draw_command, width, height);
            }
            last_hotspot_counters_.gpu_present_region_count = 0;
            last_hotspot_counters_.gpu_present_path = GpuPresentPath::FullRedrawCopyBack;
            last_hotspot_counters_.gpu_present_tradeoff = GpuPresentTradeoff::None;
            last_hotspot_counters_.gpu_estimated_draw_pixel_count =
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 2;
        }
        [scene_encoder endEncoding];

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = drawable.texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(1.0, 1.0, 1.0, 1.0);
        id<MTLRenderCommandEncoder> encoder =
            [command_buffer renderCommandEncoderWithDescriptor:pass];
        if (encoder == nil) {
            software_->present(surface);
            return;
        }
        [encoder setRenderPipelineState:texture_pipeline_];
        [encoder setVertexBuffer:vertex_buffer_ offset:0 atIndex:0];
        [encoder setFragmentTexture:scene_texture_ atIndex:0];
        if (sampler_state_ != nil) {
            [encoder setFragmentSamplerState:sampler_state_ atIndex:0];
        }
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        ++last_hotspot_counters_.gpu_draw_call_count;
        [encoder endEncoding];

        scene_initialized_ = true;
        last_hotspot_counters_.gpu_viewport_pixel_count =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

        [command_buffer presentDrawable:drawable];
        [command_buffer commit];
    }

    [[nodiscard]] RenderHotspotCounters last_hotspot_counters() const override {
        return last_hotspot_counters_;
    }

private:
    [[nodiscard]] bool ensure_scene_texture(int width, int height) {
        if (device_ == nil || width <= 0 || height <= 0) {
            return false;
        }

        if (scene_texture_ != nil && static_cast<int>(scene_texture_.width) == width &&
            static_cast<int>(scene_texture_.height) == height) {
            return true;
        }

        MTLTextureDescriptor* descriptor =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:static_cast<NSUInteger>(width)
                                                              height:static_cast<NSUInteger>(height)
                                                           mipmapped:NO];
        descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        descriptor.storageMode = MTLStorageModePrivate;
        scene_texture_ = [device_ newTextureWithDescriptor:descriptor];
        scene_initialized_ = false;
        return scene_texture_ != nil;
    }

    [[nodiscard]] Rect scaled_command_bounds(const DrawCommand& draw_command) const {
        switch (draw_command.kind) {
        case DrawCommandKind::Primitive:
            return scale_rect(primitive_commands_[draw_command.command_index].rect, scale_factor_);
        case DrawCommandKind::Image:
            return scale_rect(image_commands_[draw_command.command_index].rect, scale_factor_);
        case DrawCommandKind::Text:
            return scale_rect(text_commands_[draw_command.command_index].rect, scale_factor_);
        }
        return {};
    }

    [[nodiscard]] bool draw_command_intersects_damage(const DrawCommand& draw_command,
                                                      Rect damage_rect) const {
        return rects_intersect(scaled_command_bounds(draw_command), damage_rect);
    }

    [[nodiscard]] MTLScissorRect scissor_rect(Rect rect, int width, int height) const {
        const NSUInteger x = static_cast<NSUInteger>(
            std::clamp(static_cast<int>(std::floor(rect.x)), 0, std::max(0, width)));
        const NSUInteger y = static_cast<NSUInteger>(
            std::clamp(static_cast<int>(std::floor(rect.y)), 0, std::max(0, height)));
        const NSUInteger right = static_cast<NSUInteger>(
            std::clamp(static_cast<int>(std::ceil(rect.right())), 0, std::max(0, width)));
        const NSUInteger bottom = static_cast<NSUInteger>(
            std::clamp(static_cast<int>(std::ceil(rect.bottom())), 0, std::max(0, height)));
        return MTLScissorRect{
            x,
            y,
            right > x ? right - x : 0,
            bottom > y ? bottom - y : 0,
        };
    }

    void encode_clear_rect(id<MTLRenderCommandEncoder> encoder,
                           id<MTLRenderPipelineState>& active_pipeline,
                           Rect rect,
                           int width,
                           int height) {
        if (encoder == nil || rect.width <= 0.0F || rect.height <= 0.0F) {
            return;
        }

        if (active_pipeline != primitive_pipeline_) {
            [encoder setRenderPipelineState:primitive_pipeline_];
            [encoder setVertexBuffer:primitive_vertex_buffer_ offset:0 atIndex:0];
            active_pipeline = primitive_pipeline_;
        }

        PrimitiveUniforms uniforms{};
        uniforms.rect[0] = rect.x;
        uniforms.rect[1] = rect.y;
        uniforms.rect[2] = rect.width;
        uniforms.rect[3] = rect.height;
        uniforms.color[0] = 1.0F;
        uniforms.color[1] = 1.0F;
        uniforms.color[2] = 1.0F;
        uniforms.color[3] = 1.0F;
        uniforms.viewport_size[0] = static_cast<float>(width);
        uniforms.viewport_size[1] = static_cast<float>(height);
        [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
        [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        ++last_hotspot_counters_.gpu_draw_call_count;
    }

    void encode_draw_command(id<MTLRenderCommandEncoder> encoder,
                             id<MTLRenderPipelineState>& active_pipeline,
                             const DrawCommand& draw_command,
                             int width,
                             int height) {
        switch (draw_command.kind) {
        case DrawCommandKind::Primitive: {
            const auto& command = primitive_commands_[draw_command.command_index];
            if (active_pipeline != primitive_pipeline_) {
                [encoder setRenderPipelineState:primitive_pipeline_];
                [encoder setVertexBuffer:primitive_vertex_buffer_ offset:0 atIndex:0];
                active_pipeline = primitive_pipeline_;
            }

            PrimitiveUniforms uniforms{};
            const Rect scaled_rect = scale_rect(command.rect, scale_factor_);
            uniforms.rect[0] = scaled_rect.x;
            uniforms.rect[1] = scaled_rect.y;
            uniforms.rect[2] = scaled_rect.width;
            uniforms.rect[3] = scaled_rect.height;
            uniforms.color[0] = command.color.r;
            uniforms.color[1] = command.color.g;
            uniforms.color[2] = command.color.b;
            uniforms.color[3] = command.color.a;
            uniforms.radius = command.radius * scale_factor_;
            uniforms.thickness = command.thickness * scale_factor_;
            uniforms.kind = command.kind;
            populate_clip_uniforms(uniforms, command.clips, command.clip_count, scale_factor_);
            uniforms.viewport_size[0] = static_cast<float>(width);
            uniforms.viewport_size[1] = static_cast<float>(height);
            [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
            [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            ++last_hotspot_counters_.gpu_draw_call_count;
            break;
        }
        case DrawCommandKind::Image: {
            const auto& command = image_commands_[draw_command.command_index];
            id<MTLTexture> image_texture = upload_image_texture(command);
            if (image_texture == nil) {
                break;
            }

            if (active_pipeline != image_pipeline_) {
                [encoder setRenderPipelineState:image_pipeline_];
                [encoder setVertexBuffer:primitive_vertex_buffer_ offset:0 atIndex:0];
                active_pipeline = image_pipeline_;
            }

            ImageUniforms uniforms{};
            const Rect scaled_rect = scale_rect(command.rect, scale_factor_);
            uniforms.rect[0] = scaled_rect.x;
            uniforms.rect[1] = scaled_rect.y;
            uniforms.rect[2] = scaled_rect.width;
            uniforms.rect[3] = scaled_rect.height;
            populate_clip_uniforms(uniforms, command.clips, command.clip_count, scale_factor_);
            uniforms.viewport_size[0] = static_cast<float>(width);
            uniforms.viewport_size[1] = static_cast<float>(height);
            [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
            [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
            [encoder setFragmentTexture:image_texture atIndex:0];
            [encoder setFragmentSamplerState:(command.scale_mode == ScaleMode::Bilinear
                                                  ? linear_sampler_state_
                                                  : sampler_state_)
                                     atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            ++last_hotspot_counters_.gpu_draw_call_count;
            break;
        }
        case DrawCommandKind::Text: {
            const auto& command = text_commands_[draw_command.command_index];
            id<MTLTexture> text_texture = upload_text_texture(command);
            if (text_texture == nil) {
                break;
            }

            if (active_pipeline != image_pipeline_) {
                [encoder setRenderPipelineState:image_pipeline_];
                [encoder setVertexBuffer:primitive_vertex_buffer_ offset:0 atIndex:0];
                active_pipeline = image_pipeline_;
            }

            ImageUniforms uniforms{};
            const Rect scaled_rect = scale_rect(command.rect, scale_factor_);
            uniforms.rect[0] = scaled_rect.x;
            uniforms.rect[1] = scaled_rect.y;
            uniforms.rect[2] = scaled_rect.width;
            uniforms.rect[3] = scaled_rect.height;
            populate_clip_uniforms(uniforms, command.clips, command.clip_count, scale_factor_);
            uniforms.viewport_size[0] = static_cast<float>(width);
            uniforms.viewport_size[1] = static_cast<float>(height);
            [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:1];
            [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:1];
            [encoder setFragmentTexture:text_texture atIndex:0];
            [encoder setFragmentSamplerState:sampler_state_ atIndex:0];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            ++last_hotspot_counters_.gpu_draw_call_count;
            break;
        }
        }
    }

    template <typename Map> void trim_texture_cache(Map& cache, std::size_t max_entries) {
        for (auto it = cache.begin(); it != cache.end();) {
            if ((frame_serial_ - it->second.last_used_frame) > kTextureCacheMaxAgeFrames) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }

        while (cache.size() > max_entries) {
            auto oldest = cache.begin();
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                if (it->second.last_used_frame < oldest->second.last_used_frame) {
                    oldest = it;
                }
            }
            cache.erase(oldest);
        }
    }

    id<MTLTexture> upload_image_texture(const ImageCommand& command) {
        if (device_ == nil || command.pixel_data == nullptr || command.src_width <= 0 ||
            command.src_height <= 0) {
            return nil;
        }

        const std::size_t byte_count = static_cast<std::size_t>(command.src_width) *
                                       static_cast<std::size_t>(command.src_height) * 4;
        const ImageTextureCacheKey cache_key{
            .pixel_data = command.pixel_data,
            .width = command.src_width,
            .height = command.src_height,
            .content_hash =
                hash_bytes(reinterpret_cast<const uint8_t*>(command.pixel_data), byte_count),
        };
        if (auto it = image_texture_cache_.find(cache_key); it != image_texture_cache_.end()) {
            it->second.last_used_frame = frame_serial_;
            return it->second.texture;
        }

        const std::size_t pixel_count = static_cast<std::size_t>(command.src_width) *
                                        static_cast<std::size_t>(command.src_height);
        image_upload_buffer_.resize(pixel_count * 4);
        for (std::size_t index = 0; index < pixel_count; ++index) {
            const uint32_t pixel = command.pixel_data[index];
            image_upload_buffer_[(index * 4) + 0] = static_cast<uint8_t>((pixel >> 16) & 0xFF);
            image_upload_buffer_[(index * 4) + 1] = static_cast<uint8_t>((pixel >> 8) & 0xFF);
            image_upload_buffer_[(index * 4) + 2] = static_cast<uint8_t>(pixel & 0xFF);
            image_upload_buffer_[(index * 4) + 3] = static_cast<uint8_t>((pixel >> 24) & 0xFF);
        }

        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:static_cast<NSUInteger>(command.src_width)
                                        height:static_cast<NSUInteger>(command.src_height)
                                     mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderRead;
        descriptor.storageMode = MTLStorageModeShared;
        id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
        if (texture == nil) {
            NK_LOG_ERROR("MetalRenderer", "Failed to allocate Metal image texture");
            return nil;
        }

        const MTLRegion region = MTLRegionMake2D(0,
                                                 0,
                                                 static_cast<NSUInteger>(command.src_width),
                                                 static_cast<NSUInteger>(command.src_height));
        [texture replaceRegion:region
                   mipmapLevel:0
                     withBytes:image_upload_buffer_.data()
                   bytesPerRow:static_cast<NSUInteger>(command.src_width * 4)];
        ++last_hotspot_counters_.image_texture_upload_count;
        image_texture_cache_[cache_key] = {.texture = texture, .last_used_frame = frame_serial_};
        return texture;
    }

    id<MTLTexture> upload_text_texture(const TextCommand& command) {
        if (device_ == nil || command.shaped_text == nullptr ||
            command.shaped_text->bitmap_data() == nullptr ||
            command.shaped_text->bitmap_width() <= 0 || command.shaped_text->bitmap_height() <= 0) {
            return nil;
        }

        const TextTextureCacheKey cache_key{
            .shaped_text = command.shaped_text.get(),
            .bitmap_data = command.shaped_text->bitmap_data(),
            .width = command.shaped_text->bitmap_width(),
            .height = command.shaped_text->bitmap_height(),
        };
        if (auto it = text_texture_cache_.find(cache_key); it != text_texture_cache_.end()) {
            it->second.last_used_frame = frame_serial_;
            return it->second.texture;
        }

        MTLTextureDescriptor* descriptor = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                         width:static_cast<NSUInteger>(
                                                   command.shaped_text->bitmap_width())
                                        height:static_cast<NSUInteger>(
                                                   command.shaped_text->bitmap_height())
                                     mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderRead;
        descriptor.storageMode = MTLStorageModeShared;
        id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
        if (texture == nil) {
            NK_LOG_ERROR("MetalRenderer", "Failed to allocate Metal text texture");
            return nil;
        }

        const MTLRegion region =
            MTLRegionMake2D(0,
                            0,
                            static_cast<NSUInteger>(command.shaped_text->bitmap_width()),
                            static_cast<NSUInteger>(command.shaped_text->bitmap_height()));
        [texture replaceRegion:region
                   mipmapLevel:0
                     withBytes:command.shaped_text->bitmap_data()
                   bytesPerRow:static_cast<NSUInteger>(command.shaped_text->bitmap_width() * 4)];
        ++last_hotspot_counters_.text_texture_upload_count;
        text_texture_cache_[cache_key] = {.texture = texture, .last_used_frame = frame_serial_};
        return texture;
    }

    std::shared_ptr<ShapedText> shape_text_node(const TextNode& text_node) {
        if (text_shaper_ == nullptr || text_node.text().empty()) {
            return nullptr;
        }

        auto font = text_node.font();
        font.size *= scale_factor_;
        const auto color = text_node.text_color();
        TextKey key{
            .text = text_node.text(),
            .family = font.family,
            .size = font.size,
            .weight = static_cast<int>(font.weight),
            .style = static_cast<int>(font.style),
            .r = color.r,
            .g = color.g,
            .b = color.b,
            .a = color.a,
            .scale = scale_factor_,
        };

        if (auto it = shaped_text_cache_.find(key); it != shaped_text_cache_.end()) {
            ++last_hotspot_counters_.text_shape_cache_hit_count;
            return it->second;
        }

        if (shaped_text_cache_.size() >= 1024) {
            shaped_text_cache_.clear();
        }

        auto shaped =
            std::make_shared<ShapedText>(text_shaper_->shape(text_node.text(), font, color));
        shaped_text_cache_[key] = shaped;
        ++last_hotspot_counters_.text_shape_count;
        return shaped;
    }

    bool collect_draw_commands(const RenderNode& node) {
        switch (node.kind()) {
        case RenderNodeKind::Container:
            for (const auto& child : node.children()) {
                if (child != nullptr && !collect_draw_commands(*child)) {
                    return false;
                }
            }
            return true;
        case RenderNodeKind::ColorRect: {
            const auto& color_node = static_cast<const ColorRectNode&>(node);
            PrimitiveCommand command{
                .rect = color_node.bounds(),
                .color = color_node.color(),
                .radius = 0.0F,
                .thickness = 0.0F,
                .kind = 0,
            };
            command.clip_count = static_cast<uint32_t>(active_clips_.size());
            std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
            primitive_commands_.push_back(command);
            draw_commands_.push_back({.kind = DrawCommandKind::Primitive,
                                      .command_index = primitive_commands_.size() - 1});
            return true;
        }
        case RenderNodeKind::RoundedRect: {
            const auto& rounded_node = static_cast<const RoundedRectNode&>(node);
            PrimitiveCommand command{
                .rect = rounded_node.bounds(),
                .color = rounded_node.color(),
                .radius =
                    effective_corner_radius(rounded_node.bounds(), rounded_node.corner_radius()),
                .thickness = 0.0F,
                .kind = 0,
            };
            command.clip_count = static_cast<uint32_t>(active_clips_.size());
            std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
            primitive_commands_.push_back(command);
            draw_commands_.push_back({.kind = DrawCommandKind::Primitive,
                                      .command_index = primitive_commands_.size() - 1});
            return true;
        }
        case RenderNodeKind::Border: {
            const auto& border_node = static_cast<const BorderNode&>(node);
            PrimitiveCommand command{
                .rect = border_node.bounds(),
                .color = border_node.color(),
                .radius =
                    effective_corner_radius(border_node.bounds(), border_node.corner_radius()),
                .thickness = std::max(0.0F, border_node.thickness()),
                .kind = 1,
            };
            command.clip_count = static_cast<uint32_t>(active_clips_.size());
            std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
            primitive_commands_.push_back(command);
            draw_commands_.push_back({.kind = DrawCommandKind::Primitive,
                                      .command_index = primitive_commands_.size() - 1});
            return true;
        }
        case RenderNodeKind::RoundedClip: {
            if (active_clips_.size() >= 4) {
                return false;
            }
            const auto& clip_node = static_cast<const RoundedClipNode&>(node);
            active_clips_.push_back({
                .rect = clip_node.bounds(),
                .radius = effective_corner_radius(clip_node.bounds(), clip_node.corner_radius()),
            });
            for (const auto& child : node.children()) {
                if (child != nullptr && !collect_draw_commands(*child)) {
                    active_clips_.pop_back();
                    return false;
                }
            }
            active_clips_.pop_back();
            return true;
        }
        case RenderNodeKind::Image: {
            const auto& image_node = static_cast<const ImageNode&>(node);
            if (image_node.pixel_data() == nullptr || image_node.src_width() <= 0 ||
                image_node.src_height() <= 0) {
                return true;
            }

            ++last_hotspot_counters_.image_node_count;
            last_hotspot_counters_.image_source_pixel_count +=
                static_cast<std::size_t>(image_node.src_width()) *
                static_cast<std::size_t>(image_node.src_height());
            last_hotspot_counters_.image_dest_pixel_count +=
                scaled_pixel_area(image_node.bounds(), scale_factor_);

            ImageCommand command{
                .rect = image_node.bounds(),
                .pixel_data = image_node.pixel_data(),
                .src_width = image_node.src_width(),
                .src_height = image_node.src_height(),
                .scale_mode = image_node.scale_mode(),
            };
            command.clip_count = static_cast<uint32_t>(active_clips_.size());
            std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
            image_commands_.push_back(command);
            draw_commands_.push_back(
                {.kind = DrawCommandKind::Image, .command_index = image_commands_.size() - 1});
            return true;
        }
        case RenderNodeKind::Text: {
            const auto& text_node = static_cast<const TextNode&>(node);
            ++last_hotspot_counters_.text_node_count;
            auto shaped_text = shape_text_node(text_node);
            if (shaped_text == nullptr || shaped_text->bitmap_data() == nullptr ||
                shaped_text->bitmap_width() <= 0 || shaped_text->bitmap_height() <= 0) {
                return true;
            }

            last_hotspot_counters_.text_bitmap_pixel_count +=
                static_cast<std::size_t>(shaped_text->bitmap_width()) *
                static_cast<std::size_t>(shaped_text->bitmap_height());

            TextCommand command{
                .rect = {text_node.bounds().x,
                         text_node.bounds().y,
                         shaped_text->bitmap_width() / scale_factor_,
                         shaped_text->bitmap_height() / scale_factor_},
                .shaped_text = std::move(shaped_text),
            };
            command.clip_count = static_cast<uint32_t>(active_clips_.size());
            std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
            text_commands_.push_back(std::move(command));
            draw_commands_.push_back(
                {.kind = DrawCommandKind::Text, .command_index = text_commands_.size() - 1});
            return true;
        }
        }
        return false;
    }

    id<MTLDevice> device_ = nil;
    id<MTLCommandQueue> command_queue_ = nil;
    id<MTLLibrary> library_ = nil;
    id<MTLRenderPipelineState> texture_pipeline_ = nil;
    id<MTLRenderPipelineState> image_pipeline_ = nil;
    id<MTLRenderPipelineState> primitive_pipeline_ = nil;
    id<MTLBuffer> vertex_buffer_ = nil;
    id<MTLBuffer> primitive_vertex_buffer_ = nil;
    id<MTLSamplerState> sampler_state_ = nil;
    id<MTLSamplerState> linear_sampler_state_ = nil;
    id<MTLTexture> staging_texture_ = nil;
    id<MTLTexture> scene_texture_ = nil;
    CAMetalLayer* layer_ = nil;
    std::unique_ptr<SoftwareRenderer> software_;
    TextShaper* text_shaper_ = nullptr;
    std::unordered_map<TextKey, std::shared_ptr<ShapedText>, TextKeyHash> shaped_text_cache_;
    std::unordered_map<ImageTextureCacheKey, TextureCacheEntry, ImageTextureCacheKeyHash>
        image_texture_cache_;
    std::unordered_map<TextTextureCacheKey, TextureCacheEntry, TextTextureCacheKeyHash>
        text_texture_cache_;
    std::vector<ClipRegion> active_clips_;
    std::vector<DrawCommand> draw_commands_;
    std::vector<PrimitiveCommand> primitive_commands_;
    std::vector<ImageCommand> image_commands_;
    std::vector<TextCommand> text_commands_;
    std::vector<uint8_t> image_upload_buffer_;
    std::vector<Rect> damage_regions_;
    Size logical_viewport_{};
    float scale_factor_ = 1.0F;
    uint64_t frame_serial_ = 0;
    bool full_redraw_ = true;
    bool scene_initialized_ = false;
    bool needs_software_fallback_ = false;
    RenderHotspotCounters last_hotspot_counters_{};
};

std::unique_ptr<Renderer> create_metal_renderer() {
    auto renderer = std::make_unique<MetalRenderer>();
    return renderer;
}

} // namespace nk

#endif
