#include "directx_device.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <memory>
#include <nk/platform/platform_backend.h>
#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <nk/render/renderer.h>
#include <nk/text/shaped_text.h>
#include <nk/text/text_shaper.h>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

namespace nk {

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t kMaxClipDepth = 8;
constexpr uint64_t kTextureCacheMaxAgeFrames = 120;
constexpr std::size_t kImageTextureCacheMaxEntries = 128;
constexpr std::size_t kTextTextureCacheMaxEntries = 256;

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
    float blur = 0.0F;
    std::vector<Point> points;
    bool closed = false;
    std::array<ClipRegion, kMaxClipDepth> clips{};
    uint32_t clip_count = 0;
};

struct GradientCommand {
    Rect rect{};
    Color start_color{};
    Color end_color{};
    Orientation direction = Orientation::Vertical;
    std::array<ClipRegion, kMaxClipDepth> clips{};
    uint32_t clip_count = 0;
};

struct ImageCommand {
    float opacity = 1.0F;
    Rect rect{};
    const uint32_t* pixel_data = nullptr;
    int src_width = 0;
    int src_height = 0;
    ScaleMode scale_mode = ScaleMode::NearestNeighbor;
    std::array<ClipRegion, kMaxClipDepth> clips{};
    uint32_t clip_count = 0;
};

struct TextCommand {
    float opacity = 1.0F;
    Rect rect{};
    std::shared_ptr<ShapedText> shaped_text;
    std::array<ClipRegion, kMaxClipDepth> clips{};
    uint32_t clip_count = 0;
};

enum class DrawCommandKind : uint8_t {
    Primitive,
    Gradient,
    Image,
    Text,
};

struct DrawCommand {
    DrawCommandKind kind = DrawCommandKind::Primitive;
    std::size_t command_index = 0;
};

struct DrawConstants {
    float rect[4]{};
    float color[4]{};
    float clip_rects[kMaxClipDepth * 4]{};
    float clip_radii[kMaxClipDepth]{};
    float params0[4]{};
    float viewport[4]{};
    float effects[4]{};
    float points[128 * 4]{};
};

static_assert(sizeof(DrawConstants) ==
              16 + 16 + (kMaxClipDepth * 16) + (kMaxClipDepth * 4) + 16 + 16 + 16 + 128 * 16);
static_assert(sizeof(DrawConstants) % 16 == 0,
              "Constant buffer size must be a multiple of 16 bytes");

struct GradientDrawConstants {
    float rect[4]{};
    float start_color[4]{};
    float end_color[4]{};
    float clip_rects[kMaxClipDepth * 4]{};
    float clip_radii[kMaxClipDepth]{};
    float params0[4]{};
    float viewport[4]{};
};

static_assert(sizeof(GradientDrawConstants) ==
              16 + 16 + 16 + (kMaxClipDepth * 16) + (kMaxClipDepth * 4) + 16 + 16);
static_assert(sizeof(GradientDrawConstants) % 16 == 0,
              "Constant buffer size must be a multiple of 16 bytes");

struct ImageDrawConstants {
    float rect[4]{};
    float clip_rects[kMaxClipDepth * 4]{};
    float clip_radii[kMaxClipDepth]{};
    float params0[4]{};
    float viewport[4]{};
};

static_assert(sizeof(ImageDrawConstants) ==
              16 + (kMaxClipDepth * 16) + (kMaxClipDepth * 4) + 16 + 16);
static_assert(sizeof(ImageDrawConstants) % 16 == 0,
              "Constant buffer size must be a multiple of 16 bytes");

struct ImageTextureCacheKey {
    const uint32_t* pixel_data = nullptr;
    int width = 0;
    int height = 0;
    std::size_t content_hash = 0;

    bool operator==(const ImageTextureCacheKey& other) const = default;
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

struct ImageTextureCacheEntry {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> shader_resource_view;
    uint64_t last_used_frame = 0;
    bool opaque = false;
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
    float max_width = 0.0F;
    float scale = 1.0F;

    bool operator==(const TextKey& other) const = default;
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
        hash ^= std::hash<float>{}(key.max_width) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        hash ^= std::hash<float>{}(key.scale) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
        return hash;
    }
};

struct TextTextureCacheKey {
    const ShapedText* shaped_text = nullptr;
    const uint8_t* bitmap_data = nullptr;
    int width = 0;
    int height = 0;

    bool operator==(const TextTextureCacheKey& other) const = default;
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

struct TextTextureCacheEntry {
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> shader_resource_view;
    uint64_t last_used_frame = 0;
};

constexpr std::string_view kPrimitiveVertexShader = R"(
cbuffer DrawConstantsBuffer : register(b0) {
    float4 rect;
    float4 color;
    float4 clip_rects[8];
    float4 clip_radii[2];
    float4 params0;
    float4 viewport;
    float4 effects;
    float4 points[128];
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 pixel : TEXCOORD0;
};

VertexOutput main(uint vertex_id : SV_VertexID) {
    const float2 unit_positions[4] = {
        float2(0.0, 0.0),
        float2(1.0, 0.0),
        float2(0.0, 1.0),
        float2(1.0, 1.0),
    };
    float2 pixel_position = rect.xy + (rect.zw * unit_positions[vertex_id]);
    float2 ndc;
    ndc.x = (pixel_position.x / max(viewport.x, 1.0)) * 2.0 - 1.0;
    ndc.y = 1.0 - ((pixel_position.y / max(viewport.y, 1.0)) * 2.0);

    VertexOutput output;
    output.position = float4(ndc, 0.0, 1.0);
    output.pixel = pixel_position;
    return output;
}
)";

constexpr std::string_view kPrimitivePixelShader = R"(
cbuffer DrawConstantsBuffer : register(b0) {
    float4 rect;
    float4 color;
    float4 clip_rects[8];
    float4 clip_radii[2];
    float4 params0;
    float4 viewport;
    float4 effects;
    float4 points[128];
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 pixel : TEXCOORD0;
};

float rounded_rect_sd(float2 pixel_position, float4 rect_value, float radius) {
    float clamped_radius = clamp(radius, 0.0, min(rect_value.z, rect_value.w) * 0.5);
    float2 center = rect_value.xy + rect_value.zw * 0.5;
    float2 local = abs(pixel_position - center);
    float2 half_size = rect_value.zw * 0.5;
    float2 inner = max(half_size - clamped_radius, float2(0.0, 0.0));
    float2 q = local - inner;
    float outside = length(max(q, float2(0.0, 0.0)));
    float inside = min(max(q.x, q.y), 0.0);
    return outside + inside - clamped_radius;
}

float clip_coverage(float2 pixel_position, uint clip_count) {
    float coverage = 1.0;
    for (uint clip_index = 0; clip_index < clip_count; ++clip_index) {
        float radius = clip_radii[clip_index / 4][clip_index % 4];
        coverage *= saturate(
            0.5 - rounded_rect_sd(pixel_position, clip_rects[clip_index], radius));
    }
    return coverage;
}

float4 main(VertexOutput input) : SV_Target {
    float radius = params0.x;
    float thickness = params0.y;
    uint kind = (uint)params0.z;
    uint clip_count = (uint)params0.w;

    float coverage = saturate(0.5 - rounded_rect_sd(input.pixel, rect, radius));

    if (kind == 1u) {
        float inner_radius = max(0.0, radius - thickness);
        float4 inner_rect = float4(rect.x + thickness,
                                   rect.y + thickness,
                                   max(0.0, rect.z - (thickness * 2.0)),
                                   max(0.0, rect.w - (thickness * 2.0)));
        coverage = saturate(
            coverage - saturate(0.5 - rounded_rect_sd(input.pixel, inner_rect, inner_radius)));
    }
    if (kind == 2u || kind == 4u) {
        uint count = (uint)effects.y;
        uint segments = count == 1u ? 1u : count - 1u + (uint)effects.z;
        float distance_to_path = 1e20;
        for (uint i = 0; i < segments; ++i) {
            float2 a = points[i % count].xy;
            float2 b = points[(i + 1u) % count].xy;
            float2 d = b - a;
            float t = saturate(dot(input.pixel - a, d) / max(dot(d, d), 1e-20));
            distance_to_path = min(distance_to_path, length(input.pixel - a - t * d));
        }
        coverage = saturate(max(0.5, thickness * 0.5) + 0.5 - distance_to_path);
    } else if (kind == 3u) {
        float blur = effects.x;
        float4 shape = float4(rect.xy + blur, max(0.0, rect.zw - 2.0 * blur));
        float distance_to_shape = rounded_rect_sd(input.pixel, shape, radius);
        coverage = blur > 0.0 ? saturate(1.0 - max(0.0, distance_to_shape) / blur)
                              : (distance_to_shape < 0.0 ? 1.0 : 0.0);
    }
    coverage *= clip_coverage(input.pixel, clip_count);
    return float4(color.rgb, color.a * coverage);
}
)";

constexpr std::string_view kGradientVertexShader = R"(
cbuffer GradientDrawConstantsBuffer : register(b0) {
    float4 rect;
    float4 start_color;
    float4 end_color;
    float4 clip_rects[8];
    float4 clip_radii[2];
    float4 params0;
    float4 viewport;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 pixel : TEXCOORD0;
};

VertexOutput main(uint vertex_id : SV_VertexID) {
    const float2 unit_positions[4] = {
        float2(0.0, 0.0),
        float2(1.0, 0.0),
        float2(0.0, 1.0),
        float2(1.0, 1.0),
    };
    float2 pixel_position = rect.xy + (rect.zw * unit_positions[vertex_id]);
    float2 ndc;
    ndc.x = (pixel_position.x / max(viewport.x, 1.0)) * 2.0 - 1.0;
    ndc.y = 1.0 - ((pixel_position.y / max(viewport.y, 1.0)) * 2.0);

    VertexOutput output;
    output.position = float4(ndc, 0.0, 1.0);
    output.pixel = pixel_position;
    return output;
}
)";

constexpr std::string_view kGradientPixelShader = R"(
cbuffer GradientDrawConstantsBuffer : register(b0) {
    float4 rect;
    float4 start_color;
    float4 end_color;
    float4 clip_rects[8];
    float4 clip_radii[2];
    float4 params0;
    float4 viewport;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 pixel : TEXCOORD0;
};

float rounded_rect_sd(float2 pixel_position, float4 rect_value, float radius) {
    float clamped_radius = clamp(radius, 0.0, min(rect_value.z, rect_value.w) * 0.5);
    float2 center = rect_value.xy + rect_value.zw * 0.5;
    float2 local = abs(pixel_position - center);
    float2 half_size = rect_value.zw * 0.5;
    float2 inner = max(half_size - clamped_radius, float2(0.0, 0.0));
    float2 q = local - inner;
    float outside = length(max(q, float2(0.0, 0.0)));
    float inside = min(max(q.x, q.y), 0.0);
    return outside + inside - clamped_radius;
}

float clip_coverage(float2 pixel_position, uint clip_count) {
    float coverage = 1.0;
    for (uint clip_index = 0; clip_index < clip_count; ++clip_index) {
        float radius = clip_radii[clip_index / 4][clip_index % 4];
        coverage *= saturate(
            0.5 - rounded_rect_sd(pixel_position, clip_rects[clip_index], radius));
    }
    return coverage;
}

float4 main(VertexOutput input) : SV_Target {
    bool horizontal = params0.x > 0.5;
    uint clip_count = (uint)params0.w;
    float span = max(horizontal ? rect.z : rect.w, 1.0);
    float position = horizontal ? (input.pixel.x - rect.x) : (input.pixel.y - rect.y);
    float t = saturate(position / span);
    float4 gradient_color = lerp(start_color, end_color, t);
    float coverage = saturate(0.5 - rounded_rect_sd(input.pixel, rect, 0.0));
    coverage *= clip_coverage(input.pixel, clip_count);
    return float4(gradient_color.rgb, gradient_color.a * coverage);
}
)";

constexpr std::string_view kImageVertexShader = R"(
cbuffer ImageDrawConstantsBuffer : register(b0) {
    float4 rect;
    float4 clip_rects[8];
    float4 clip_radii[2];
    float4 params0;
    float4 viewport;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 pixel : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

VertexOutput main(uint vertex_id : SV_VertexID) {
    const float2 unit_positions[4] = {
        float2(0.0, 0.0),
        float2(1.0, 0.0),
        float2(0.0, 1.0),
        float2(1.0, 1.0),
    };
    float2 unit = unit_positions[vertex_id];
    float2 pixel_position = rect.xy + (rect.zw * unit);
    float2 ndc;
    ndc.x = (pixel_position.x / max(viewport.x, 1.0)) * 2.0 - 1.0;
    ndc.y = 1.0 - ((pixel_position.y / max(viewport.y, 1.0)) * 2.0);

    VertexOutput output;
    output.position = float4(ndc, 0.0, 1.0);
    output.pixel = pixel_position;
    output.uv = unit;
    return output;
}
)";

constexpr std::string_view kImagePixelShader = R"(
Texture2D image_texture : register(t0);
SamplerState image_sampler : register(s0);

cbuffer ImageDrawConstantsBuffer : register(b0) {
    float4 rect;
    float4 clip_rects[8];
    float4 clip_radii[2];
    float4 params0;
    float4 viewport;
};

struct VertexOutput {
    float4 position : SV_Position;
    float2 pixel : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

float rounded_rect_sd(float2 pixel_position, float4 rect_value, float radius) {
    float clamped_radius = clamp(radius, 0.0, min(rect_value.z, rect_value.w) * 0.5);
    float2 center = rect_value.xy + rect_value.zw * 0.5;
    float2 local = abs(pixel_position - center);
    float2 half_size = rect_value.zw * 0.5;
    float2 inner = max(half_size - clamped_radius, float2(0.0, 0.0));
    float2 q = local - inner;
    float outside = length(max(q, float2(0.0, 0.0)));
    float inside = min(max(q.x, q.y), 0.0);
    return outside + inside - clamped_radius;
}

float clip_coverage(float2 pixel_position) {
    float coverage = 1.0;
    for (uint clip_index = 0; clip_index < 8; ++clip_index) {
        if (clip_rects[clip_index].z <= 0.0 || clip_rects[clip_index].w <= 0.0) {
            break;
        }
        float radius = clip_radii[clip_index / 4][clip_index % 4];
        coverage *= saturate(
            0.5 - rounded_rect_sd(pixel_position, clip_rects[clip_index], radius));
    }
    return coverage;
}

float4 main(VertexOutput input) : SV_Target {
    float4 sampled = image_texture.Sample(image_sampler, input.uv);
    sampled.a *= clip_coverage(input.pixel) * params0.x;
    return sampled;
}
)";

HRESULT compile_shader(std::string_view source,
                       const char* entry_point,
                       const char* target,
                       ComPtr<ID3DBlob>& bytecode);

float normalize_scale_factor(float scale_factor);
Rect scale_rect(Rect rect, float scale_factor);
bool rect_is_empty(Rect rect);
Rect clip_rect_to_viewport(Rect rect, int width, int height);
bool rects_overlap_or_touch(Rect lhs, Rect rhs);
Rect union_rect(Rect lhs, Rect rhs);
std::size_t pixel_area(Rect rect);
float effective_corner_radius(Rect rect, float requested_radius);
D3D11_RECT to_d3d_rect(Rect rect);
D3D11_BOX to_d3d_box(Rect rect);
RECT to_win32_rect(Rect rect);
std::size_t hash_image_content(const uint32_t* pixel_data, int width, int height);

FontDescriptor renderer_font_for_shape(FontDescriptor font, float scale_factor) {
#if defined(_WIN32)
    (void)scale_factor;
    return font;
#else
    font.size *= scale_factor;
    return font;
#endif
}

float renderer_max_width_for_shape(float max_width, float scale_factor) {
#if defined(_WIN32)
    (void)scale_factor;
    return max_width;
#else
    return max_width * scale_factor;
#endif
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif

template <typename T> const IID& type_iid() {
    return __uuidof(T);
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

class DirectXRenderer final : public Renderer {
public:
    explicit DirectXRenderer(RendererBackend backend);
    ~DirectXRenderer() override;

    [[nodiscard]] RendererBackend backend() const override;
    bool attach_surface(NativeSurface& surface) override;
    void set_text_shaper(TextShaper* shaper) override;
    void set_damage_regions(std::span<const Rect> regions) override;
    void begin_frame(Size viewport, float scale_factor) override;
    void render(const RenderNode& root) override;
    void end_frame() override;
    void present(NativeSurface& surface) override;
    [[nodiscard]] RenderHotspotCounters last_hotspot_counters() const override;
    [[nodiscard]] std::optional<FramePixels> read_back_frame() override;

private:
    template <typename Map> void trim_texture_cache(Map& cache, std::size_t max_entries);
    bool collect_gpu_commands(const RenderNode& node);
    std::shared_ptr<ShapedText> shape_text_node(const TextNode& text_node);
    bool ensure_software_frame();
    bool create_device_objects();
    bool create_pipeline_objects();
    bool ensure_render_targets();
    void destroy_context();
    void destroy_device_objects();
    ImageTextureCacheEntry* ensure_image_texture(const ImageCommand& command);
    TextTextureCacheEntry* ensure_text_texture(const TextCommand& command);
    bool draw_gpu_scene();
    bool present_scene();
    bool present_uploaded_software_frame();
    void present_software_direct(NativeSurface& surface);
    [[nodiscard]] Rect effective_command_bounds(const PrimitiveCommand& command) const;
    [[nodiscard]] Rect effective_command_bounds(const GradientCommand& command) const;
    [[nodiscard]] Rect effective_command_bounds(const ImageCommand& command) const;
    [[nodiscard]] Rect effective_command_bounds(const TextCommand& command) const;
    [[nodiscard]] bool primitive_covers_damage(const PrimitiveCommand& command, Rect damage) const;
    [[nodiscard]] bool damage_requires_clear(Rect damage) const;
    bool command_intersects_damage(const PrimitiveCommand& command, Rect damage) const;
    bool command_intersects_damage(const GradientCommand& command, Rect damage) const;
    bool command_intersects_damage(const ImageCommand& command, Rect damage) const;
    bool command_intersects_damage(const TextCommand& command, Rect damage) const;
    void draw_clear_rect(Rect damage);
    void draw_primitive(const PrimitiveCommand& command,
                        std::optional<Rect> scissor,
                        bool already_scaled = false);
    void draw_gradient(const GradientCommand& command, std::optional<Rect> scissor);
    void draw_image(const ImageCommand& command,
                    ID3D11ShaderResourceView& shader_resource_view,
                    std::optional<Rect> scissor,
                    float opacity = 1.0f);
    void draw_text(const TextCommand& command,
                   ID3D11ShaderResourceView& shader_resource_view,
                   std::optional<Rect> scissor);

    std::unique_ptr<SoftwareRenderer> software_;
    TextShaper* text_shaper_ = nullptr;
    float active_opacity_ = 1.0F;
    NativeSurface* attached_surface_ = nullptr;
    HWND hwnd_ = nullptr;
    RendererBackend backend_;
    DirectXDevice gpu_;
    int target_width_ = 0;
    int target_height_ = 0;
    bool presented_software_ = false;
    ComPtr<ID3D11Texture2D> scene_texture_;
    ComPtr<ID3D11RenderTargetView> scene_render_target_;
    ComPtr<ID3D11Texture2D> software_texture_;
    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> pixel_shader_;
    ComPtr<ID3D11VertexShader> gradient_vertex_shader_;
    ComPtr<ID3D11PixelShader> gradient_pixel_shader_;
    ComPtr<ID3D11VertexShader> image_vertex_shader_;
    ComPtr<ID3D11PixelShader> image_pixel_shader_;
    ComPtr<ID3D11Buffer> constant_buffer_;
    ComPtr<ID3D11Buffer> gradient_constant_buffer_;
    ComPtr<ID3D11BlendState> blend_state_;
    ComPtr<ID3D11RasterizerState> rasterizer_state_;
    ComPtr<ID3D11SamplerState> nearest_sampler_state_;
    ComPtr<ID3D11SamplerState> linear_sampler_state_;
    Size logical_viewport_{};
    float scale_factor_ = 1.0F;
    int framebuffer_width_ = 0;
    int framebuffer_height_ = 0;
    uint64_t frame_serial_ = 0;
    const RenderNode* last_root_ = nullptr;
    std::vector<Rect> logical_damage_regions_;
    std::vector<Rect> pixel_damage_regions_;
    std::vector<DrawCommand> draw_commands_;
    std::vector<PrimitiveCommand> primitive_commands_;
    std::vector<GradientCommand> gradient_commands_;
    std::vector<ImageCommand> image_commands_;
    std::vector<TextCommand> text_commands_;
    std::vector<ClipRegion> active_clips_;
    std::vector<uint8_t> converted_pixels_;
    std::unordered_map<TextKey, std::shared_ptr<ShapedText>, TextKeyHash> shaped_text_cache_;
    std::unordered_map<ImageTextureCacheKey, ImageTextureCacheEntry, ImageTextureCacheKeyHash>
        image_texture_cache_;
    std::unordered_map<TextTextureCacheKey, TextTextureCacheEntry, TextTextureCacheKeyHash>
        text_texture_cache_;
    RenderHotspotCounters last_hotspot_counters_{};
    bool full_redraw_ = true;
    bool use_gpu_scene_ = false;
    bool previous_gpu_scene_ = false;
    bool scene_initialized_ = false;
    bool full_present_ = true;
    bool software_frame_rendered_ = false;
    bool software_frame_finalized_ = false;
};

HRESULT compile_shader(std::string_view source,
                       const char* entry_point,
                       const char* target,
                       ComPtr<ID3DBlob>& bytecode) {
    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
    flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    ComPtr<ID3DBlob> errors;
    return D3DCompile(source.data(),
                      source.size(),
                      nullptr,
                      nullptr,
                      nullptr,
                      entry_point,
                      target,
                      flags,
                      0,
                      &bytecode,
                      &errors);
}

float normalize_scale_factor(float scale_factor) {
    return std::isfinite(scale_factor) && scale_factor > 0.0F ? scale_factor : 1.0F;
}

Rect scale_rect(Rect rect, float scale_factor) {
    return {
        rect.x * scale_factor,
        rect.y * scale_factor,
        rect.width * scale_factor,
        rect.height * scale_factor,
    };
}

bool rect_is_empty(Rect rect) {
    return rect.width <= 0.0F || rect.height <= 0.0F;
}

Rect clip_rect_to_viewport(Rect rect, int width, int height) {
    const float x0 = std::clamp(rect.x, 0.0F, static_cast<float>(width));
    const float y0 = std::clamp(rect.y, 0.0F, static_cast<float>(height));
    const float x1 = std::clamp(rect.right(), 0.0F, static_cast<float>(width));
    const float y1 = std::clamp(rect.bottom(), 0.0F, static_cast<float>(height));
    return {x0, y0, std::max(0.0F, x1 - x0), std::max(0.0F, y1 - y0)};
}

bool rects_overlap_or_touch(Rect lhs, Rect rhs) {
    return lhs.x <= rhs.right() && rhs.x <= lhs.right() && lhs.y <= rhs.bottom() &&
           rhs.y <= lhs.bottom();
}

Rect union_rect(Rect lhs, Rect rhs) {
    const float x0 = std::min(lhs.x, rhs.x);
    const float y0 = std::min(lhs.y, rhs.y);
    const float x1 = std::max(lhs.right(), rhs.right());
    const float y1 = std::max(lhs.bottom(), rhs.bottom());
    return {x0, y0, std::max(0.0F, x1 - x0), std::max(0.0F, y1 - y0)};
}

Rect intersect_rect(Rect lhs, Rect rhs) {
    const float x0 = std::max(lhs.x, rhs.x);
    const float y0 = std::max(lhs.y, rhs.y);
    const float x1 = std::min(lhs.right(), rhs.right());
    const float y1 = std::min(lhs.bottom(), rhs.bottom());
    return {x0, y0, std::max(0.0F, x1 - x0), std::max(0.0F, y1 - y0)};
}

std::size_t pixel_area(Rect rect) {
    if (rect_is_empty(rect)) {
        return 0;
    }
    const int width = std::max(1, static_cast<int>(std::lround(rect.width)));
    const int height = std::max(1, static_cast<int>(std::lround(rect.height)));
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

float effective_corner_radius(Rect rect, float requested_radius) {
    return std::clamp(requested_radius, 0.0F, std::min(rect.width, rect.height) * 0.5F);
}

bool rounded_rect_contains_point(Rect rect, float radius, Point point) {
    if (!rect.contains(point) &&
        !(point.x == rect.right() && point.y >= rect.y && point.y <= rect.bottom()) &&
        !(point.y == rect.bottom() && point.x >= rect.x && point.x <= rect.right())) {
        return false;
    }

    const float clamped_radius = effective_corner_radius(rect, radius);
    if (clamped_radius <= 0.0F) {
        return true;
    }

    const float inner_left = rect.x + clamped_radius;
    const float inner_right = rect.right() - clamped_radius;
    const float inner_top = rect.y + clamped_radius;
    const float inner_bottom = rect.bottom() - clamped_radius;
    if ((point.x >= inner_left && point.x <= inner_right) ||
        (point.y >= inner_top && point.y <= inner_bottom)) {
        return true;
    }

    const float center_x = point.x < inner_left ? inner_left : inner_right;
    const float center_y = point.y < inner_top ? inner_top : inner_bottom;
    const float dx = point.x - center_x;
    const float dy = point.y - center_y;
    return (dx * dx + dy * dy) <= (clamped_radius * clamped_radius);
}

D3D11_RECT to_d3d_rect(Rect rect) {
    return {
        static_cast<LONG>(std::floor(rect.x)),
        static_cast<LONG>(std::floor(rect.y)),
        static_cast<LONG>(std::ceil(rect.right())),
        static_cast<LONG>(std::ceil(rect.bottom())),
    };
}

D3D11_BOX to_d3d_box(Rect rect) {
    return {
        static_cast<UINT>(std::floor(rect.x)),
        static_cast<UINT>(std::floor(rect.y)),
        0U,
        static_cast<UINT>(std::ceil(rect.right())),
        static_cast<UINT>(std::ceil(rect.bottom())),
        1U,
    };
}

RECT to_win32_rect(Rect rect) {
    return {
        static_cast<LONG>(std::floor(rect.x)),
        static_cast<LONG>(std::floor(rect.y)),
        static_cast<LONG>(std::ceil(rect.right())),
        static_cast<LONG>(std::ceil(rect.bottom())),
    };
}

std::size_t hash_image_content(const uint32_t* pixel_data, int width, int height) {
    if (pixel_data == nullptr || width <= 0 || height <= 0) {
        return 0;
    }

    // FNV-1a over the pixel bytes. The accumulator must stay 64-bit even on
    // 32-bit Windows, where std::size_t cannot hold the FNV primes; the
    // narrowing return only changes the hash's width, not its distribution
    // over a session.
    constexpr std::uint64_t kOffsetBasis = 1469598103934665603ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t hash = kOffsetBasis;
    const auto* bytes = reinterpret_cast<const uint8_t*>(pixel_data);
    const auto size = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4;
    for (std::uint64_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::uint64_t>(bytes[index]);
        hash *= kPrime;
    }
    return static_cast<std::size_t>(hash);
}

DirectXRenderer::DirectXRenderer(RendererBackend backend)
    : software_(std::make_unique<SoftwareRenderer>()), backend_(backend) {}

DirectXRenderer::~DirectXRenderer() {
    destroy_context();
}

RendererBackend DirectXRenderer::backend() const {
    return backend_;
}

bool DirectXRenderer::attach_surface(NativeSurface& surface) {
    (void)software_->attach_surface(surface);

    auto* hwnd = static_cast<HWND>(surface.native_handle());
    if (hwnd == nullptr) {
        destroy_context();
        attached_surface_ = nullptr;
        return false;
    }

    attached_surface_ = &surface;
    if (hwnd_ == hwnd && gpu_.healthy()) {
        return true;
    }

    hwnd_ = hwnd;
    destroy_device_objects();
    return create_device_objects();
}

void DirectXRenderer::set_text_shaper(TextShaper* shaper) {
    text_shaper_ = shaper;
    software_->set_text_shaper(shaper);
}

void DirectXRenderer::set_damage_regions(std::span<const Rect> regions) {
    logical_damage_regions_.assign(regions.begin(), regions.end());
    pixel_damage_regions_.clear();
    full_redraw_ = true;

    if (framebuffer_width_ <= 0 || framebuffer_height_ <= 0) {
        last_hotspot_counters_.damage_region_count = 0;
        return;
    }

    for (const auto& region : regions) {
        const auto clipped = clip_rect_to_viewport(
            scale_rect(region, scale_factor_), framebuffer_width_, framebuffer_height_);
        if (rect_is_empty(clipped)) {
            continue;
        }

        bool merged = false;
        for (auto& existing : pixel_damage_regions_) {
            if (rects_overlap_or_touch(existing, clipped)) {
                existing = union_rect(existing, clipped);
                merged = true;
                break;
            }
        }

        if (!merged) {
            pixel_damage_regions_.push_back(clipped);
        }
    }

    for (std::size_t outer = 0; outer < pixel_damage_regions_.size(); ++outer) {
        for (std::size_t inner = outer + 1; inner < pixel_damage_regions_.size();) {
            if (rects_overlap_or_touch(pixel_damage_regions_[outer],
                                       pixel_damage_regions_[inner])) {
                pixel_damage_regions_[outer] =
                    union_rect(pixel_damage_regions_[outer], pixel_damage_regions_[inner]);
                pixel_damage_regions_.erase(pixel_damage_regions_.begin() +
                                            static_cast<std::ptrdiff_t>(inner));
            } else {
                ++inner;
            }
        }
    }

    if (!pixel_damage_regions_.empty()) {
        full_redraw_ = false;
    }
    last_hotspot_counters_.damage_region_count = pixel_damage_regions_.size();
}

void DirectXRenderer::begin_frame(Size viewport, float scale_factor) {
    logical_viewport_ = viewport;
    scale_factor_ = normalize_scale_factor(scale_factor);
    if (text_shaper_ != nullptr) {
        text_shaper_->set_scale_factor(scale_factor_);
    }
    ++frame_serial_;

    const auto framebuffer = attached_surface_ != nullptr
                                 ? attached_surface_->framebuffer_size()
                                 : Size{std::round(viewport.width * scale_factor_),
                                        std::round(viewport.height * scale_factor_)};
    framebuffer_width_ = std::max(1, static_cast<int>(std::lround(framebuffer.width)));
    framebuffer_height_ = std::max(1, static_cast<int>(std::lround(framebuffer.height)));

    if (shaped_text_cache_.size() >= 1024) {
        shaped_text_cache_.clear();
    }
    trim_texture_cache(image_texture_cache_, kImageTextureCacheMaxEntries);
    trim_texture_cache(text_texture_cache_, kTextTextureCacheMaxEntries);
    draw_commands_.clear();
    primitive_commands_.clear();
    gradient_commands_.clear();
    image_commands_.clear();
    text_commands_.clear();
    active_clips_.clear();
    active_opacity_ = 1.0F;
    last_root_ = nullptr;
    previous_gpu_scene_ = use_gpu_scene_;
    use_gpu_scene_ = false;
    software_frame_rendered_ = false;
    software_frame_finalized_ = false;
    full_present_ = true;
    last_hotspot_counters_ = {};
    last_hotspot_counters_.gpu_viewport_pixel_count = static_cast<std::size_t>(framebuffer_width_) *
                                                      static_cast<std::size_t>(framebuffer_height_);
}

void DirectXRenderer::render(const RenderNode& root) {
    last_root_ = &root;
    draw_commands_.clear();
    primitive_commands_.clear();
    gradient_commands_.clear();
    image_commands_.clear();
    text_commands_.clear();
    active_clips_.clear();
    active_opacity_ = 1.0F;
    use_gpu_scene_ = collect_gpu_commands(root);
    if (use_gpu_scene_ != previous_gpu_scene_) {
        full_redraw_ = true;
        scene_initialized_ = false;
    }

    if (use_gpu_scene_) {
        if (full_redraw_ || pixel_damage_regions_.empty() || !scene_initialized_) {
            last_hotspot_counters_.gpu_estimated_draw_pixel_count =
                last_hotspot_counters_.gpu_viewport_pixel_count;
        } else {
            std::size_t damage_pixels = 0;
            for (const auto& region : pixel_damage_regions_) {
                damage_pixels += pixel_area(region);
            }
            last_hotspot_counters_.gpu_estimated_draw_pixel_count = damage_pixels;
        }
        return;
    }

    ensure_software_frame();
}

void DirectXRenderer::end_frame() {
    if (software_frame_rendered_ && !software_frame_finalized_) {
        software_->end_frame();
        software_frame_finalized_ = true;
    }
}

template <typename Map>
void DirectXRenderer::trim_texture_cache(Map& cache, std::size_t max_entries) {
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

bool DirectXRenderer::collect_gpu_commands(const RenderNode& node) {
    switch (node.kind()) {
    case RenderNodeKind::Transform:
        return false;
    case RenderNodeKind::Line: {
        const auto& line = static_cast<const LineNode&>(node);
        PrimitiveCommand command;
        command.rect = node.bounds();
        command.color = line.color();
        command.color.a *= active_opacity_;
        command.thickness = line.thickness();
        command.kind = 2;
        command.points = {line.start(), line.end()};
        command.clip_count = static_cast<uint32_t>(active_clips_.size());
        std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
        primitive_commands_.push_back(std::move(command));
        draw_commands_.push_back({DrawCommandKind::Primitive, primitive_commands_.size() - 1});
        return true;
    }
    case RenderNodeKind::Path: {
        const auto& path = static_cast<const PathNode&>(node);
        if (path.path().points.empty()) {
            return true;
        }
        if (path.path().points.size() > 128) {
            return false;
        }
        PrimitiveCommand command;
        command.rect = node.bounds();
        command.color = path.stroke_color();
        command.color.a *= active_opacity_;
        command.thickness = path.thickness();
        command.kind = 4;
        command.points = path.path().points;
        command.closed = path.path().closed;
        command.clip_count = static_cast<uint32_t>(active_clips_.size());
        std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
        primitive_commands_.push_back(std::move(command));
        draw_commands_.push_back({DrawCommandKind::Primitive, primitive_commands_.size() - 1});
        return true;
    }
    case RenderNodeKind::Shadow: {
        const auto& shadow = static_cast<const ShadowNode&>(node);
        PrimitiveCommand command;
        command.rect = node.bounds();
        command.color = shadow.color();
        command.color.a *= active_opacity_;
        command.radius = shadow.corner_radius();
        command.blur = shadow.blur_radius();
        command.kind = 3;
        command.clip_count = static_cast<uint32_t>(active_clips_.size());
        std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
        primitive_commands_.push_back(std::move(command));
        draw_commands_.push_back({DrawCommandKind::Primitive, primitive_commands_.size() - 1});
        return true;
    }
    case RenderNodeKind::Container:
        for (const auto& child : node.children()) {
            if (child != nullptr && !collect_gpu_commands(*child)) {
                primitive_commands_.clear();
                gradient_commands_.clear();
                image_commands_.clear();
                text_commands_.clear();
                draw_commands_.clear();
                return false;
            }
        }
        return true;
    case RenderNodeKind::ColorRect: {
        const auto& color_node = static_cast<const ColorRectNode&>(node);
        PrimitiveCommand command;
        command.rect = color_node.bounds();
        command.color = color_node.color();
        command.color.a *= active_opacity_;
        command.clip_count = static_cast<uint32_t>(active_clips_.size());
        std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
        primitive_commands_.push_back(command);
        draw_commands_.push_back(
            {.kind = DrawCommandKind::Primitive, .command_index = primitive_commands_.size() - 1});
        return true;
    }
    case RenderNodeKind::RoundedRect: {
        const auto& rounded_node = static_cast<const RoundedRectNode&>(node);
        PrimitiveCommand command;
        command.rect = rounded_node.bounds();
        command.color = rounded_node.color();
        command.color.a *= active_opacity_;
        command.radius =
            effective_corner_radius(rounded_node.bounds(), rounded_node.corner_radius());
        command.clip_count = static_cast<uint32_t>(active_clips_.size());
        std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
        primitive_commands_.push_back(command);
        draw_commands_.push_back(
            {.kind = DrawCommandKind::Primitive, .command_index = primitive_commands_.size() - 1});
        return true;
    }
    case RenderNodeKind::Border: {
        const auto& border_node = static_cast<const BorderNode&>(node);
        PrimitiveCommand command;
        command.rect = border_node.bounds();
        command.color = border_node.color();
        command.color.a *= active_opacity_;
        command.radius = effective_corner_radius(border_node.bounds(), border_node.corner_radius());
        command.thickness = std::max(0.0F, border_node.thickness());
        command.kind = 1;
        command.clip_count = static_cast<uint32_t>(active_clips_.size());
        std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
        primitive_commands_.push_back(command);
        draw_commands_.push_back(
            {.kind = DrawCommandKind::Primitive, .command_index = primitive_commands_.size() - 1});
        return true;
    }
    case RenderNodeKind::LinearGradient: {
        const auto& gradient_node = static_cast<const LinearGradientNode&>(node);
        GradientCommand command{
            .rect = gradient_node.bounds(),
            .start_color = gradient_node.start_color(),
            .end_color = gradient_node.end_color(),
            .direction = gradient_node.direction(),
        };
        command.clip_count = static_cast<uint32_t>(active_clips_.size());
        std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
        command.start_color.a *= active_opacity_;
        command.end_color.a *= active_opacity_;
        gradient_commands_.push_back(command);
        draw_commands_.push_back(
            {.kind = DrawCommandKind::Gradient, .command_index = gradient_commands_.size() - 1});
        return true;
    }
    case RenderNodeKind::RoundedClip: {
        if (rect_is_empty(node.bounds())) {
            return true;
        }
        if (active_clips_.size() >= kMaxClipDepth) {
            primitive_commands_.clear();
            gradient_commands_.clear();
            image_commands_.clear();
            text_commands_.clear();
            draw_commands_.clear();
            return false;
        }

        const auto& clip_node = static_cast<const RoundedClipNode&>(node);
        active_clips_.push_back(
            {.rect = clip_node.bounds(),
             .radius = effective_corner_radius(clip_node.bounds(), clip_node.corner_radius())});
        for (const auto& child : node.children()) {
            if (child != nullptr && !collect_gpu_commands(*child)) {
                active_clips_.pop_back();
                primitive_commands_.clear();
                gradient_commands_.clear();
                image_commands_.clear();
                text_commands_.clear();
                draw_commands_.clear();
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
            pixel_area(scale_rect(image_node.bounds(), scale_factor_));

        ImageCommand command{
            .rect = image_node.bounds(),
            .pixel_data = image_node.pixel_data(),
            .src_width = image_node.src_width(),
            .src_height = image_node.src_height(),
            .scale_mode = image_node.scale_mode(),
        };
        command.clip_count = static_cast<uint32_t>(active_clips_.size());
        std::copy(active_clips_.begin(), active_clips_.end(), command.clips.begin());
        command.opacity = active_opacity_;
        image_commands_.push_back(command);
        draw_commands_.push_back(
            {.kind = DrawCommandKind::Image, .command_index = image_commands_.size() - 1});
        return true;
    }
    case RenderNodeKind::Opacity: {
        const auto& opacity_node = static_cast<const OpacityNode&>(node);
        const float previous_opacity = active_opacity_;
        active_opacity_ *= std::clamp(opacity_node.opacity(), 0.0F, 1.0F);
        for (const auto& child : node.children()) {
            if (child != nullptr && !collect_gpu_commands(*child)) {
                active_opacity_ = previous_opacity;
                return false;
            }
        }
        active_opacity_ = previous_opacity;
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
        command.opacity = active_opacity_;
        text_commands_.push_back(std::move(command));
        draw_commands_.push_back(
            {.kind = DrawCommandKind::Text, .command_index = text_commands_.size() - 1});
        return true;
    }
    default:
        primitive_commands_.clear();
        gradient_commands_.clear();
        image_commands_.clear();
        text_commands_.clear();
        draw_commands_.clear();
        return false;
    }
}

bool DirectXRenderer::ensure_software_frame() {
    if (software_frame_rendered_) {
        return true;
    }

    software_->begin_frame(logical_viewport_, scale_factor_);
    software_->set_damage_regions(full_redraw_ ? std::span<const Rect>{}
                                               : std::span<const Rect>{logical_damage_regions_});
    if (last_root_ != nullptr) {
        software_->render(*last_root_);
    }
    software_frame_rendered_ = true;
    last_hotspot_counters_ = software_->last_hotspot_counters();
    return true;
}

std::shared_ptr<ShapedText> DirectXRenderer::shape_text_node(const TextNode& text_node) {
    if (text_shaper_ == nullptr || text_node.text().empty()) {
        return nullptr;
    }

    auto font = renderer_font_for_shape(text_node.font(), scale_factor_);
    const auto color = text_node.text_color();
    const float max_width = text_node.max_width() > 0.0F
                                ? renderer_max_width_for_shape(text_node.max_width(), scale_factor_)
                                : 0.0F;
    const TextKey key{
        .text = text_node.text(),
        .family = font.family,
        .size = font.size,
        .weight = static_cast<int>(font.weight),
        .style = static_cast<int>(font.style),
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
        .max_width = max_width,
        .scale = scale_factor_,
    };

    if (auto it = shaped_text_cache_.find(key); it != shaped_text_cache_.end()) {
        ++last_hotspot_counters_.text_shape_cache_hit_count;
        return it->second;
    }

    auto shaped = std::make_shared<ShapedText>(
        max_width > 0.0F ? text_shaper_->shape_wrapped(text_node.text(), font, color, max_width)
                         : text_shaper_->shape(text_node.text(), font, color));
    ++last_hotspot_counters_.text_shape_count;
    shaped_text_cache_[key] = shaped;
    return shaped;
}

bool DirectXRenderer::create_device_objects() {
    destroy_device_objects();
    return gpu_.create(hwnd_, backend_ == RendererBackend::D3D12) && create_pipeline_objects();
}

bool DirectXRenderer::create_pipeline_objects() {
    ComPtr<ID3DBlob> vertex_bytecode;
    ComPtr<ID3DBlob> pixel_bytecode;
    ComPtr<ID3DBlob> gradient_vertex_bytecode;
    ComPtr<ID3DBlob> gradient_pixel_bytecode;
    ComPtr<ID3DBlob> image_vertex_bytecode;
    ComPtr<ID3DBlob> image_pixel_bytecode;
    if (FAILED(compile_shader(kPrimitiveVertexShader, "main", "vs_5_0", vertex_bytecode)) ||
        FAILED(compile_shader(kPrimitivePixelShader, "main", "ps_5_0", pixel_bytecode)) ||
        FAILED(compile_shader(kGradientVertexShader, "main", "vs_5_0", gradient_vertex_bytecode)) ||
        FAILED(compile_shader(kGradientPixelShader, "main", "ps_5_0", gradient_pixel_bytecode)) ||
        FAILED(compile_shader(kImageVertexShader, "main", "vs_5_0", image_vertex_bytecode)) ||
        FAILED(compile_shader(kImagePixelShader, "main", "ps_5_0", image_pixel_bytecode))) {
        destroy_device_objects();
        return false;
    }

    if (FAILED(gpu_.device->CreateVertexShader(vertex_bytecode->GetBufferPointer(),
                                               vertex_bytecode->GetBufferSize(),
                                               nullptr,
                                               &vertex_shader_)) ||
        FAILED(gpu_.device->CreatePixelShader(pixel_bytecode->GetBufferPointer(),
                                              pixel_bytecode->GetBufferSize(),
                                              nullptr,
                                              &pixel_shader_)) ||
        FAILED(gpu_.device->CreateVertexShader(gradient_vertex_bytecode->GetBufferPointer(),
                                               gradient_vertex_bytecode->GetBufferSize(),
                                               nullptr,
                                               &gradient_vertex_shader_)) ||
        FAILED(gpu_.device->CreatePixelShader(gradient_pixel_bytecode->GetBufferPointer(),
                                              gradient_pixel_bytecode->GetBufferSize(),
                                              nullptr,
                                              &gradient_pixel_shader_)) ||
        FAILED(gpu_.device->CreateVertexShader(image_vertex_bytecode->GetBufferPointer(),
                                               image_vertex_bytecode->GetBufferSize(),
                                               nullptr,
                                               &image_vertex_shader_)) ||
        FAILED(gpu_.device->CreatePixelShader(image_pixel_bytecode->GetBufferPointer(),
                                              image_pixel_bytecode->GetBufferSize(),
                                              nullptr,
                                              &image_pixel_shader_))) {
        destroy_device_objects();
        return false;
    }

    D3D11_BUFFER_DESC constant_buffer_desc{};
    constant_buffer_desc.ByteWidth = sizeof(DrawConstants);
    constant_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(gpu_.device->CreateBuffer(&constant_buffer_desc, nullptr, &constant_buffer_))) {
        destroy_device_objects();
        return false;
    }

    D3D11_BUFFER_DESC gradient_constant_buffer_desc{};
    gradient_constant_buffer_desc.ByteWidth = sizeof(GradientDrawConstants);
    gradient_constant_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    gradient_constant_buffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(gpu_.device->CreateBuffer(
            &gradient_constant_buffer_desc, nullptr, &gradient_constant_buffer_))) {
        destroy_device_objects();
        return false;
    }

    D3D11_BLEND_DESC blend_desc{};
    blend_desc.RenderTarget[0].BlendEnable = TRUE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(gpu_.device->CreateBlendState(&blend_desc, &blend_state_))) {
        destroy_device_objects();
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizer_desc{};
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.ScissorEnable = TRUE;
    rasterizer_desc.DepthClipEnable = TRUE;
    if (FAILED(gpu_.device->CreateRasterizerState(&rasterizer_desc, &rasterizer_state_))) {
        destroy_device_objects();
        return false;
    }

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(gpu_.device->CreateSamplerState(&sampler_desc, &nearest_sampler_state_))) {
        destroy_device_objects();
        return false;
    }

    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (FAILED(gpu_.device->CreateSamplerState(&sampler_desc, &linear_sampler_state_))) {
        destroy_device_objects();
        return false;
    }

    return true;
}

bool DirectXRenderer::ensure_render_targets() {
    if (gpu_.device == nullptr || gpu_.swap_chain == nullptr || attached_surface_ == nullptr) {
        return false;
    }

    const auto framebuffer = attached_surface_->framebuffer_size();
    const int width = std::max(1, static_cast<int>(std::lround(framebuffer.width)));
    const int height = std::max(1, static_cast<int>(std::lround(framebuffer.height)));
    if (width == target_width_ && height == target_height_ && scene_texture_ != nullptr) {
        return true;
    }

    framebuffer_width_ = width;
    framebuffer_height_ = height;

    if (gpu_.context != nullptr) {
        ID3D11RenderTargetView* null_targets[] = {nullptr};
        gpu_.context->OMSetRenderTargets(1, null_targets, nullptr);
    }

    scene_render_target_.Reset();
    scene_texture_.Reset();
    software_texture_.Reset();

    if (!gpu_.resize(width, height)) {
        return false;
    }
    target_width_ = width;
    target_height_ = height;
    full_redraw_ = true;

    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = static_cast<UINT>(width);
    texture_desc.Height = static_cast<UINT>(height);
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(gpu_.device->CreateTexture2D(&texture_desc, nullptr, &scene_texture_))) {
        return false;
    }
    if (FAILED(gpu_.device->CreateRenderTargetView(
            scene_texture_.Get(), nullptr, &scene_render_target_))) {
        return false;
    }

    texture_desc.BindFlags = 0;
    if (FAILED(gpu_.device->CreateTexture2D(&texture_desc, nullptr, &software_texture_))) {
        return false;
    }

    scene_initialized_ = false;
    return true;
}

void DirectXRenderer::destroy_context() {
    destroy_device_objects();
    hwnd_ = nullptr;
    attached_surface_ = nullptr;
    scene_initialized_ = false;
    framebuffer_width_ = 0;
    framebuffer_height_ = 0;
}

void DirectXRenderer::destroy_device_objects() {
    if (gpu_.context != nullptr) {
        ID3D11RenderTargetView* null_targets[] = {nullptr};
        gpu_.context->OMSetRenderTargets(1, null_targets, nullptr);
        gpu_.context->ClearState();
        gpu_.context->Flush();
    }

    software_texture_.Reset();
    scene_render_target_.Reset();
    scene_texture_.Reset();
    constant_buffer_.Reset();
    gradient_constant_buffer_.Reset();
    blend_state_.Reset();
    rasterizer_state_.Reset();
    linear_sampler_state_.Reset();
    nearest_sampler_state_.Reset();
    image_pixel_shader_.Reset();
    image_vertex_shader_.Reset();
    gradient_pixel_shader_.Reset();
    gradient_vertex_shader_.Reset();
    pixel_shader_.Reset();
    vertex_shader_.Reset();
    shaped_text_cache_.clear();
    image_texture_cache_.clear();
    text_texture_cache_.clear();
    gpu_.reset();
    target_width_ = 0;
    target_height_ = 0;
    scene_initialized_ = false;
}

void DirectXRenderer::present(NativeSurface& surface) {
    presented_software_ = false;
    if (!attach_surface(surface) || !ensure_render_targets()) {
        present_software_direct(surface);
        return;
    }

    if (use_gpu_scene_) {
        if (!draw_gpu_scene() || !present_scene()) {
            present_software_direct(surface);
        }
        return;
    }

    if (!ensure_software_frame()) {
        present_software_direct(surface);
        return;
    }
    if (!software_frame_finalized_) {
        software_->end_frame();
        software_frame_finalized_ = true;
    }
    if (!present_uploaded_software_frame()) {
        present_software_direct(surface);
    }
}

ImageTextureCacheEntry* DirectXRenderer::ensure_image_texture(const ImageCommand& command) {
    if (gpu_.device == nullptr || gpu_.context == nullptr || command.pixel_data == nullptr ||
        command.src_width <= 0 || command.src_height <= 0) {
        return nullptr;
    }

    const ImageTextureCacheKey key{
        .pixel_data = command.pixel_data,
        .width = command.src_width,
        .height = command.src_height,
        .content_hash =
            hash_image_content(command.pixel_data, command.src_width, command.src_height),
    };

    if (auto it = image_texture_cache_.find(key); it != image_texture_cache_.end()) {
        it->second.last_used_frame = frame_serial_;
        ++last_hotspot_counters_.image_texture_cache_hit_count;
        return &it->second;
    }

    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = static_cast<UINT>(command.src_width);
    texture_desc.Height = static_cast<UINT>(command.src_height);
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial_data{};
    initial_data.pSysMem = command.pixel_data;
    initial_data.SysMemPitch = static_cast<UINT>(command.src_width * 4);

    ImageTextureCacheEntry entry;
    const auto pixel_count = static_cast<std::size_t>(command.src_width) * command.src_height;
    entry.opaque = std::all_of(command.pixel_data,
                               command.pixel_data + pixel_count,
                               [](uint32_t pixel) { return (pixel >> 24) == 255; });
    if (FAILED(gpu_.device->CreateTexture2D(&texture_desc, &initial_data, &entry.texture))) {
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_desc{};
    shader_resource_desc.Format = texture_desc.Format;
    shader_resource_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    shader_resource_desc.Texture2D.MipLevels = 1;
    if (FAILED(gpu_.device->CreateShaderResourceView(
            entry.texture.Get(), &shader_resource_desc, &entry.shader_resource_view))) {
        return nullptr;
    }

    entry.last_used_frame = frame_serial_;
    ++last_hotspot_counters_.image_texture_upload_count;
    auto [inserted, _] = image_texture_cache_.emplace(key, std::move(entry));
    return &inserted->second;
}

TextTextureCacheEntry* DirectXRenderer::ensure_text_texture(const TextCommand& command) {
    if (gpu_.device == nullptr || gpu_.context == nullptr || command.shaped_text == nullptr ||
        command.shaped_text->bitmap_data() == nullptr || command.shaped_text->bitmap_width() <= 0 ||
        command.shaped_text->bitmap_height() <= 0) {
        return nullptr;
    }

    const TextTextureCacheKey key{
        .shaped_text = command.shaped_text.get(),
        .bitmap_data = command.shaped_text->bitmap_data(),
        .width = command.shaped_text->bitmap_width(),
        .height = command.shaped_text->bitmap_height(),
    };

    if (auto it = text_texture_cache_.find(key); it != text_texture_cache_.end()) {
        it->second.last_used_frame = frame_serial_;
        ++last_hotspot_counters_.text_texture_cache_hit_count;
        return &it->second;
    }

    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = static_cast<UINT>(command.shaped_text->bitmap_width());
    texture_desc.Height = static_cast<UINT>(command.shaped_text->bitmap_height());
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial_data{};
    initial_data.pSysMem = command.shaped_text->bitmap_data();
    initial_data.SysMemPitch = static_cast<UINT>(command.shaped_text->bitmap_width() * 4);

    TextTextureCacheEntry entry;
    if (FAILED(gpu_.device->CreateTexture2D(&texture_desc, &initial_data, &entry.texture))) {
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_desc{};
    shader_resource_desc.Format = texture_desc.Format;
    shader_resource_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    shader_resource_desc.Texture2D.MipLevels = 1;
    if (FAILED(gpu_.device->CreateShaderResourceView(
            entry.texture.Get(), &shader_resource_desc, &entry.shader_resource_view))) {
        return nullptr;
    }

    entry.last_used_frame = frame_serial_;
    ++last_hotspot_counters_.text_texture_upload_count;
    auto [inserted, _] = text_texture_cache_.emplace(key, std::move(entry));
    return &inserted->second;
}

bool DirectXRenderer::draw_gpu_scene() {
    if (!ensure_render_targets() || gpu_.context == nullptr || scene_render_target_ == nullptr) {
        return false;
    }

    const bool full_scene_redraw =
        full_redraw_ || pixel_damage_regions_.empty() || !scene_initialized_;
    full_present_ = full_scene_redraw;
    const float white[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    ID3D11RenderTargetView* render_targets[] = {scene_render_target_.Get()};
    gpu_.context->OMSetRenderTargets(1, render_targets, nullptr);
    gpu_.context->IASetInputLayout(nullptr);
    gpu_.context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    gpu_.context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    gpu_.context->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    gpu_.context->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    gpu_.context->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    gpu_.context->RSSetState(rasterizer_state_.Get());
    constexpr float blend_factor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    gpu_.context->OMSetBlendState(blend_state_.Get(), blend_factor, 0xFFFFFFFFU);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(framebuffer_width_);
    viewport.Height = static_cast<float>(framebuffer_height_);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    gpu_.context->RSSetViewports(1, &viewport);

    last_hotspot_counters_.gpu_draw_call_count = 0;
    last_hotspot_counters_.gpu_replayed_command_count = 0;
    last_hotspot_counters_.gpu_skipped_command_count = 0;
    if (full_scene_redraw) {
        gpu_.context->ClearRenderTargetView(scene_render_target_.Get(), white);
        last_hotspot_counters_.gpu_replayed_command_count = draw_commands_.size();
        for (const auto& draw_command : draw_commands_) {

            if (draw_command.kind == DrawCommandKind::Primitive) {
                draw_primitive(primitive_commands_[draw_command.command_index], std::nullopt);
                continue;
            }

            if (draw_command.kind == DrawCommandKind::Gradient) {
                draw_gradient(gradient_commands_[draw_command.command_index], std::nullopt);
                continue;
            }

            if (draw_command.kind == DrawCommandKind::Image) {
                auto* texture = ensure_image_texture(image_commands_[draw_command.command_index]);
                if (texture == nullptr || texture->shader_resource_view == nullptr) {
                    return false;
                }
                draw_image(image_commands_[draw_command.command_index],
                           *texture->shader_resource_view.Get(),
                           std::nullopt,
                           1.0f);
                continue;
            }

            auto* texture = ensure_text_texture(text_commands_[draw_command.command_index]);
            if (texture == nullptr || texture->shader_resource_view == nullptr) {
                return false;
            }
            draw_text(text_commands_[draw_command.command_index],
                      *texture->shader_resource_view.Get(),
                      std::nullopt);
        }
    } else {
        for (const auto& damage : pixel_damage_regions_) {
            if (damage_requires_clear(damage)) {
                draw_clear_rect(damage);
            }
        }

        for (const auto& draw_command : draw_commands_) {
            auto collect_damage_slices = [&](Rect command_bounds) {
                std::vector<Rect> slices;
                slices.reserve(pixel_damage_regions_.size());
                for (const auto& damage : pixel_damage_regions_) {
                    const auto slice = intersect_rect(command_bounds, damage);
                    if (!rect_is_empty(slice)) {
                        slices.push_back(slice);
                    }
                }
                return slices;
            };

            auto draw_grouped_textured_command =
                [&](const auto& command, auto ensure_texture, auto draw_fn) {
                    const auto bounds = clip_rect_to_viewport(
                        effective_command_bounds(command), framebuffer_width_, framebuffer_height_);
                    const auto slices = collect_damage_slices(bounds);
                    if (slices.empty()) {
                        ++last_hotspot_counters_.gpu_skipped_command_count;
                        return true;
                    }

                    auto* texture = ensure_texture(command);
                    if (texture == nullptr || texture->shader_resource_view == nullptr) {
                        return false;
                    }

                    if (slices.size() == 1) {
                        ++last_hotspot_counters_.gpu_replayed_command_count;
                        draw_fn(command, *texture->shader_resource_view.Get(), slices.front());
                        return true;
                    }

                    Rect union_slice = slices.front();
                    std::size_t total_area = pixel_area(slices.front());
                    for (std::size_t index = 1; index < slices.size(); ++index) {
                        union_slice = union_rect(union_slice, slices[index]);
                        total_area += pixel_area(slices[index]);
                    }

                    const auto union_area = pixel_area(union_slice);
                    bool can_redraw_gap = false;
                    if constexpr (requires { texture->opaque; }) {
                        can_redraw_gap =
                            texture->opaque && command.opacity >= 1.0F && command.clip_count == 0;
                    }
                    // Replaying alpha into an untouched gap blends it a second time.
                    if (union_area <= total_area ||
                        (can_redraw_gap && union_area <= ((total_area * 5U) / 4U))) {
                        ++last_hotspot_counters_.gpu_replayed_command_count;
                        last_hotspot_counters_.gpu_skipped_command_count += slices.size() - 1U;
                        draw_fn(command, *texture->shader_resource_view.Get(), union_slice);
                        return true;
                    }

                    for (const auto& slice : slices) {
                        ++last_hotspot_counters_.gpu_replayed_command_count;
                        draw_fn(command, *texture->shader_resource_view.Get(), slice);
                    }
                    return true;
                };

            if (draw_command.kind == DrawCommandKind::Primitive) {
                for (const auto& damage : pixel_damage_regions_) {
                    const auto& command = primitive_commands_[draw_command.command_index];
                    if (!command_intersects_damage(command, damage)) {
                        ++last_hotspot_counters_.gpu_skipped_command_count;
                        continue;
                    }
                    ++last_hotspot_counters_.gpu_replayed_command_count;
                    draw_primitive(command, damage);
                }
                continue;
            }

            if (draw_command.kind == DrawCommandKind::Gradient) {
                for (const auto& damage : pixel_damage_regions_) {
                    const auto& command = gradient_commands_[draw_command.command_index];
                    if (!command_intersects_damage(command, damage)) {
                        ++last_hotspot_counters_.gpu_skipped_command_count;
                        continue;
                    }
                    ++last_hotspot_counters_.gpu_replayed_command_count;
                    draw_gradient(command, damage);
                }
                continue;
            }

            if (draw_command.kind == DrawCommandKind::Image) {
                const auto& command = image_commands_[draw_command.command_index];
                if (!draw_grouped_textured_command(
                        command,
                        [&](const ImageCommand& image_command) {
                            return ensure_image_texture(image_command);
                        },
                        [&](const ImageCommand& image_command,
                            ID3D11ShaderResourceView& view,
                            Rect scissor) { draw_image(image_command, view, scissor, 1.0f); })) {
                    return false;
                }
                continue;
            }

            const auto& command = text_commands_[draw_command.command_index];
            const auto bounds = clip_rect_to_viewport(
                effective_command_bounds(command), framebuffer_width_, framebuffer_height_);
            const auto slices = collect_damage_slices(bounds);
            if (slices.empty()) {
                ++last_hotspot_counters_.gpu_skipped_command_count;
                continue;
            }

            auto* texture = ensure_text_texture(command);
            if (texture == nullptr || texture->shader_resource_view == nullptr) {
                return false;
            }

            // Refresh the full text quad inside the scene texture on partial frames.
            // Present still copies only the dirty rects back to the swap chain, but
            // avoiding per-slice text scissoring prevents clipped glyphs when logical
            // widget damage is narrower than the shaped bitmap's real coverage.
            ++last_hotspot_counters_.gpu_replayed_command_count;
            if (slices.size() > 1) {
                last_hotspot_counters_.gpu_skipped_command_count += slices.size() - 1U;
            }
            draw_text(command, *texture->shader_resource_view.Get(), std::nullopt);
        }
    }

    scene_initialized_ = true;
    return true;
}

bool DirectXRenderer::present_scene() {
    auto* back_buffer = gpu_.acquire_back_buffer();
    if (gpu_.context == nullptr || scene_texture_ == nullptr || back_buffer == nullptr) {
        return false;
    }

    const bool full_scene_redraw =
        backend_ == RendererBackend::D3D12 || full_present_ || pixel_damage_regions_.empty();
    std::vector<RECT> dirty_rects;
    if (!full_scene_redraw) {
        dirty_rects.reserve(pixel_damage_regions_.size());
        for (const auto& region : pixel_damage_regions_) {
            const auto box = to_d3d_box(region);
            dirty_rects.push_back(to_win32_rect(region));
            gpu_.context->CopySubresourceRegion(back_buffer,
                                                0,
                                                static_cast<UINT>(std::floor(region.x)),
                                                static_cast<UINT>(std::floor(region.y)),
                                                0,
                                                scene_texture_.Get(),
                                                0,
                                                &box);
        }
    }

    if (full_scene_redraw || dirty_rects.empty()) {
        gpu_.context->CopyResource(back_buffer, scene_texture_.Get());
        last_hotspot_counters_.gpu_swapchain_copy_count = 1;
        last_hotspot_counters_.gpu_present_region_count = 0;
        last_hotspot_counters_.gpu_present_path = GpuPresentPath::FullRedrawCopyBack;
        last_hotspot_counters_.gpu_present_tradeoff = GpuPresentTradeoff::DrawFavored;
    } else {
        last_hotspot_counters_.gpu_swapchain_copy_count = dirty_rects.size();
        last_hotspot_counters_.gpu_present_region_count = dirty_rects.size();
        last_hotspot_counters_.gpu_present_path = GpuPresentPath::PartialRedrawCopy;
        last_hotspot_counters_.gpu_present_tradeoff = GpuPresentTradeoff::BandwidthFavored;
    }

    const bool presented = gpu_.present(dirty_rects);
    if (!presented) {
        destroy_device_objects();
    }
    return presented;
}

bool DirectXRenderer::present_uploaded_software_frame() {
    auto* back_buffer = gpu_.acquire_back_buffer();
    if (gpu_.context == nullptr || software_texture_ == nullptr || back_buffer == nullptr) {
        return false;
    }

    const auto* rgba = software_->pixel_data();
    const int width = software_->pixel_width();
    const int height = software_->pixel_height();
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    const auto pixel_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    converted_pixels_.resize(pixel_count * 4);
    for (std::size_t index = 0; index < pixel_count; ++index) {
        converted_pixels_[index * 4 + 0] = rgba[index * 4 + 2];
        converted_pixels_[index * 4 + 1] = rgba[index * 4 + 1];
        converted_pixels_[index * 4 + 2] = rgba[index * 4 + 0];
        converted_pixels_[index * 4 + 3] = rgba[index * 4 + 3];
    }

    gpu_.context->UpdateSubresource(software_texture_.Get(),
                                    0,
                                    nullptr,
                                    converted_pixels_.data(),
                                    static_cast<UINT>(width * 4),
                                    0);

    const bool full_scene_redraw =
        backend_ == RendererBackend::D3D12 || full_redraw_ || pixel_damage_regions_.empty();
    std::vector<RECT> dirty_rects;
    if (!full_scene_redraw) {
        dirty_rects.reserve(pixel_damage_regions_.size());
        for (const auto& region : pixel_damage_regions_) {
            const auto box = to_d3d_box(region);
            dirty_rects.push_back(to_win32_rect(region));
            gpu_.context->CopySubresourceRegion(back_buffer,
                                                0,
                                                static_cast<UINT>(std::floor(region.x)),
                                                static_cast<UINT>(std::floor(region.y)),
                                                0,
                                                software_texture_.Get(),
                                                0,
                                                &box);
        }
    }

    if (full_scene_redraw || dirty_rects.empty()) {
        gpu_.context->CopyResource(back_buffer, software_texture_.Get());
        last_hotspot_counters_.gpu_swapchain_copy_count = 1;
        last_hotspot_counters_.gpu_present_region_count = 0;
        last_hotspot_counters_.gpu_present_path = GpuPresentPath::SoftwareUpload;
        last_hotspot_counters_.gpu_present_tradeoff = GpuPresentTradeoff::None;
    } else {
        last_hotspot_counters_.gpu_swapchain_copy_count = dirty_rects.size();
        last_hotspot_counters_.gpu_present_region_count = dirty_rects.size();
        last_hotspot_counters_.gpu_present_path = GpuPresentPath::SoftwareUpload;
        last_hotspot_counters_.gpu_present_tradeoff = GpuPresentTradeoff::BandwidthFavored;
    }

    const bool presented = gpu_.present(dirty_rects);
    if (!presented) {
        destroy_device_objects();
    }
    return presented;
}

void DirectXRenderer::present_software_direct(NativeSurface& surface) {
    presented_software_ = true;
    scene_initialized_ = false;
    full_redraw_ = true;
    if (!ensure_software_frame()) {
        return;
    }
    if (!software_frame_finalized_) {
        software_->end_frame();
        software_frame_finalized_ = true;
    }
    last_hotspot_counters_ = software_->last_hotspot_counters();
    last_hotspot_counters_.gpu_present_path = GpuPresentPath::SoftwareDirect;
    last_hotspot_counters_.gpu_present_tradeoff = GpuPresentTradeoff::None;
    software_->present(surface);
}

Rect DirectXRenderer::effective_command_bounds(const PrimitiveCommand& command) const {
    Rect bounds = scale_rect(command.rect, scale_factor_);
    for (uint32_t index = 0; index < command.clip_count && index < kMaxClipDepth; ++index) {
        bounds = intersect_rect(bounds, scale_rect(command.clips[index].rect, scale_factor_));
        if (rect_is_empty(bounds)) {
            break;
        }
    }
    return bounds;
}

Rect DirectXRenderer::effective_command_bounds(const GradientCommand& command) const {
    Rect bounds = scale_rect(command.rect, scale_factor_);
    for (uint32_t index = 0; index < command.clip_count && index < kMaxClipDepth; ++index) {
        bounds = intersect_rect(bounds, scale_rect(command.clips[index].rect, scale_factor_));
        if (rect_is_empty(bounds)) {
            break;
        }
    }
    return bounds;
}

Rect DirectXRenderer::effective_command_bounds(const ImageCommand& command) const {
    Rect bounds = scale_rect(command.rect, scale_factor_);
    for (uint32_t index = 0; index < command.clip_count && index < kMaxClipDepth; ++index) {
        bounds = intersect_rect(bounds, scale_rect(command.clips[index].rect, scale_factor_));
        if (rect_is_empty(bounds)) {
            break;
        }
    }
    return bounds;
}

Rect DirectXRenderer::effective_command_bounds(const TextCommand& command) const {
    Rect bounds = scale_rect(command.rect, scale_factor_);
    for (uint32_t index = 0; index < command.clip_count && index < kMaxClipDepth; ++index) {
        bounds = intersect_rect(bounds, scale_rect(command.clips[index].rect, scale_factor_));
        if (rect_is_empty(bounds)) {
            break;
        }
    }
    return bounds;
}

bool DirectXRenderer::primitive_covers_damage(const PrimitiveCommand& command, Rect damage) const {
    if (command.kind != 0U || command.color.a < 0.999F || command.clip_count != 0U) {
        return false;
    }

    const auto bounds = clip_rect_to_viewport(
        scale_rect(command.rect, scale_factor_), framebuffer_width_, framebuffer_height_);
    if (rect_is_empty(bounds)) {
        return false;
    }
    if (damage.x < bounds.x || damage.y < bounds.y || damage.right() > bounds.right() ||
        damage.bottom() > bounds.bottom()) {
        return false;
    }

    const float radius = command.radius * scale_factor_;
    if (radius <= 0.0F) {
        return true;
    }

    const std::array<Point, 4> corners = {{
        {damage.x, damage.y},
        {damage.right(), damage.y},
        {damage.x, damage.bottom()},
        {damage.right(), damage.bottom()},
    }};
    return std::all_of(corners.begin(), corners.end(), [&](Point point) {
        return rounded_rect_contains_point(bounds, radius, point);
    });
}

bool DirectXRenderer::damage_requires_clear(Rect damage) const {
    for (const auto& draw_command : draw_commands_) {
        if (draw_command.kind != DrawCommandKind::Primitive) {
            continue;
        }

        const auto& command = primitive_commands_[draw_command.command_index];
        if (primitive_covers_damage(command, damage)) {
            return false;
        }
    }
    return true;
}

bool DirectXRenderer::command_intersects_damage(const PrimitiveCommand& command,
                                                Rect damage) const {
    const auto scaled_bounds = clip_rect_to_viewport(
        effective_command_bounds(command), framebuffer_width_, framebuffer_height_);
    return !rect_is_empty(scaled_bounds) && rects_overlap_or_touch(scaled_bounds, damage);
}

bool DirectXRenderer::command_intersects_damage(const GradientCommand& command, Rect damage) const {
    const auto scaled_bounds = clip_rect_to_viewport(
        effective_command_bounds(command), framebuffer_width_, framebuffer_height_);
    return !rect_is_empty(scaled_bounds) && rects_overlap_or_touch(scaled_bounds, damage);
}

bool DirectXRenderer::command_intersects_damage(const ImageCommand& command, Rect damage) const {
    const auto scaled_bounds = clip_rect_to_viewport(
        effective_command_bounds(command), framebuffer_width_, framebuffer_height_);
    return !rect_is_empty(scaled_bounds) && rects_overlap_or_touch(scaled_bounds, damage);
}

bool DirectXRenderer::command_intersects_damage(const TextCommand& command, Rect damage) const {
    const auto scaled_bounds = clip_rect_to_viewport(
        effective_command_bounds(command), framebuffer_width_, framebuffer_height_);
    return !rect_is_empty(scaled_bounds) && rects_overlap_or_touch(scaled_bounds, damage);
}

void DirectXRenderer::draw_clear_rect(Rect damage) {
    PrimitiveCommand clear_command;
    clear_command.rect = damage;
    clear_command.color = Color::from_rgb(255, 255, 255);
    draw_primitive(clear_command, damage, true);
}

void DirectXRenderer::draw_primitive(const PrimitiveCommand& command,
                                     std::optional<Rect> scissor,
                                     bool already_scaled) {
    DrawConstants constants{};
    const Rect draw_rect = already_scaled ? command.rect : scale_rect(command.rect, scale_factor_);
    constants.rect[0] = draw_rect.x;
    constants.rect[1] = draw_rect.y;
    constants.rect[2] = draw_rect.width;
    constants.rect[3] = draw_rect.height;
    constants.color[0] = command.color.r;
    constants.color[1] = command.color.g;
    constants.color[2] = command.color.b;
    constants.color[3] = command.color.a;
    for (uint32_t index = 0; index < command.clip_count && index < kMaxClipDepth; ++index) {
        const auto clip_rect = already_scaled
                                   ? command.clips[index].rect
                                   : scale_rect(command.clips[index].rect, scale_factor_);
        constants.clip_rects[index * 4 + 0] = clip_rect.x;
        constants.clip_rects[index * 4 + 1] = clip_rect.y;
        constants.clip_rects[index * 4 + 2] = clip_rect.width;
        constants.clip_rects[index * 4 + 3] = clip_rect.height;
        constants.clip_radii[index] = already_scaled
                                          ? command.clips[index].radius
                                          : (command.clips[index].radius * scale_factor_);
    }
    constants.params0[0] = already_scaled ? command.radius : (command.radius * scale_factor_);
    constants.params0[1] = already_scaled ? command.thickness : (command.thickness * scale_factor_);
    constants.effects[0] = command.blur * scale_factor_;
    constants.effects[1] = static_cast<float>(command.points.size());
    constants.effects[2] = command.closed ? 1.0F : 0.0F;
    for (std::size_t i = 0; i < command.points.size(); ++i) {
        constants.points[i * 4] = command.points[i].x * scale_factor_;
        constants.points[i * 4 + 1] = command.points[i].y * scale_factor_;
    }
    constants.params0[2] = static_cast<float>(command.kind);
    constants.params0[3] = static_cast<float>(command.clip_count);
    constants.viewport[0] = static_cast<float>(framebuffer_width_);
    constants.viewport[1] = static_cast<float>(framebuffer_height_);
    gpu_.context->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    gpu_.context->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    gpu_.context->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    gpu_.context->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    gpu_.context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);

    const auto scissor_rect =
        scissor.has_value()
            ? clip_rect_to_viewport(*scissor, framebuffer_width_, framebuffer_height_)
            : clip_rect_to_viewport(draw_rect, framebuffer_width_, framebuffer_height_);
    const auto d3d_scissor = to_d3d_rect(scissor_rect);
    gpu_.context->RSSetScissorRects(1, &d3d_scissor);
    ID3D11ShaderResourceView* null_views[] = {nullptr};
    gpu_.context->PSSetShaderResources(0, 1, null_views);
    gpu_.context->Draw(4, 0);
    ++last_hotspot_counters_.gpu_draw_call_count;
}

void DirectXRenderer::draw_gradient(const GradientCommand& command, std::optional<Rect> scissor) {
    GradientDrawConstants constants{};
    const Rect draw_rect = scale_rect(command.rect, scale_factor_);
    constants.rect[0] = draw_rect.x;
    constants.rect[1] = draw_rect.y;
    constants.rect[2] = draw_rect.width;
    constants.rect[3] = draw_rect.height;
    constants.start_color[0] = command.start_color.r;
    constants.start_color[1] = command.start_color.g;
    constants.start_color[2] = command.start_color.b;
    constants.start_color[3] = command.start_color.a;
    constants.end_color[0] = command.end_color.r;
    constants.end_color[1] = command.end_color.g;
    constants.end_color[2] = command.end_color.b;
    constants.end_color[3] = command.end_color.a;
    for (uint32_t index = 0; index < command.clip_count && index < kMaxClipDepth; ++index) {
        const auto clip_rect = scale_rect(command.clips[index].rect, scale_factor_);
        constants.clip_rects[index * 4 + 0] = clip_rect.x;
        constants.clip_rects[index * 4 + 1] = clip_rect.y;
        constants.clip_rects[index * 4 + 2] = clip_rect.width;
        constants.clip_rects[index * 4 + 3] = clip_rect.height;
        constants.clip_radii[index] = command.clips[index].radius * scale_factor_;
    }
    constants.params0[0] = command.direction == Orientation::Horizontal ? 1.0F : 0.0F;
    constants.params0[3] = static_cast<float>(command.clip_count);
    constants.viewport[0] = static_cast<float>(framebuffer_width_);
    constants.viewport[1] = static_cast<float>(framebuffer_height_);
    gpu_.context->VSSetShader(gradient_vertex_shader_.Get(), nullptr, 0);
    gpu_.context->PSSetShader(gradient_pixel_shader_.Get(), nullptr, 0);
    gpu_.context->VSSetConstantBuffers(0, 1, gradient_constant_buffer_.GetAddressOf());
    gpu_.context->PSSetConstantBuffers(0, 1, gradient_constant_buffer_.GetAddressOf());
    gpu_.context->UpdateSubresource(gradient_constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);

    const auto scissor_rect =
        scissor.has_value()
            ? clip_rect_to_viewport(*scissor, framebuffer_width_, framebuffer_height_)
            : clip_rect_to_viewport(draw_rect, framebuffer_width_, framebuffer_height_);
    const auto d3d_scissor = to_d3d_rect(scissor_rect);
    gpu_.context->RSSetScissorRects(1, &d3d_scissor);
    ID3D11ShaderResourceView* null_views[] = {nullptr};
    gpu_.context->PSSetShaderResources(0, 1, null_views);
    gpu_.context->Draw(4, 0);
    ++last_hotspot_counters_.gpu_draw_call_count;
}

void DirectXRenderer::draw_image(const ImageCommand& command,
                                 ID3D11ShaderResourceView& shader_resource_view,
                                 std::optional<Rect> scissor,
                                 float opacity) {
    ImageDrawConstants constants{};
    const Rect draw_rect = scale_rect(command.rect, scale_factor_);
    constants.rect[0] = draw_rect.x;
    constants.rect[1] = draw_rect.y;
    constants.rect[2] = draw_rect.width;
    constants.rect[3] = draw_rect.height;
    for (uint32_t index = 0; index < command.clip_count && index < kMaxClipDepth; ++index) {
        const auto clip_rect = scale_rect(command.clips[index].rect, scale_factor_);
        constants.clip_rects[index * 4 + 0] = clip_rect.x;
        constants.clip_rects[index * 4 + 1] = clip_rect.y;
        constants.clip_rects[index * 4 + 2] = clip_rect.width;
        constants.clip_rects[index * 4 + 3] = clip_rect.height;
        constants.clip_radii[index] = command.clips[index].radius * scale_factor_;
    }
    constants.viewport[0] = static_cast<float>(framebuffer_width_);
    constants.viewport[1] = static_cast<float>(framebuffer_height_);
    constants.params0[0] = opacity * command.opacity;
    gpu_.context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);

    const auto scissor_rect =
        scissor.has_value()
            ? clip_rect_to_viewport(*scissor, framebuffer_width_, framebuffer_height_)
            : clip_rect_to_viewport(draw_rect, framebuffer_width_, framebuffer_height_);
    const auto d3d_scissor = to_d3d_rect(scissor_rect);
    gpu_.context->RSSetScissorRects(1, &d3d_scissor);
    gpu_.context->VSSetShader(image_vertex_shader_.Get(), nullptr, 0);
    gpu_.context->PSSetShader(image_pixel_shader_.Get(), nullptr, 0);
    gpu_.context->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    gpu_.context->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    ID3D11ShaderResourceView* views[] = {&shader_resource_view};
    ID3D11SamplerState* samplers[] = {
        command.scale_mode == ScaleMode::Bilinear ? linear_sampler_state_.Get()
                                                  : nearest_sampler_state_.Get(),
    };
    gpu_.context->PSSetShaderResources(0, 1, views);
    gpu_.context->PSSetSamplers(0, 1, samplers);
    gpu_.context->Draw(4, 0);
    ID3D11ShaderResourceView* null_views[] = {nullptr};
    gpu_.context->PSSetShaderResources(0, 1, null_views);
    ++last_hotspot_counters_.gpu_draw_call_count;
}

void DirectXRenderer::draw_text(const TextCommand& command,
                                ID3D11ShaderResourceView& shader_resource_view,
                                std::optional<Rect> scissor) {
    ImageDrawConstants constants{};
    const Rect draw_rect = scale_rect(command.rect, scale_factor_);
    constants.rect[0] = draw_rect.x;
    constants.rect[1] = draw_rect.y;
    constants.rect[2] = draw_rect.width;
    constants.rect[3] = draw_rect.height;
    for (uint32_t index = 0; index < command.clip_count && index < kMaxClipDepth; ++index) {
        const auto clip_rect = scale_rect(command.clips[index].rect, scale_factor_);
        constants.clip_rects[index * 4 + 0] = clip_rect.x;
        constants.clip_rects[index * 4 + 1] = clip_rect.y;
        constants.clip_rects[index * 4 + 2] = clip_rect.width;
        constants.clip_rects[index * 4 + 3] = clip_rect.height;
        constants.clip_radii[index] = command.clips[index].radius * scale_factor_;
    }
    constants.viewport[0] = static_cast<float>(framebuffer_width_);
    constants.viewport[1] = static_cast<float>(framebuffer_height_);
    constants.params0[0] = command.opacity;
    gpu_.context->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);

    const auto scissor_rect =
        scissor.has_value()
            ? clip_rect_to_viewport(*scissor, framebuffer_width_, framebuffer_height_)
            : clip_rect_to_viewport(draw_rect, framebuffer_width_, framebuffer_height_);
    const auto d3d_scissor = to_d3d_rect(scissor_rect);
    gpu_.context->RSSetScissorRects(1, &d3d_scissor);
    gpu_.context->VSSetShader(image_vertex_shader_.Get(), nullptr, 0);
    gpu_.context->PSSetShader(image_pixel_shader_.Get(), nullptr, 0);
    gpu_.context->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    gpu_.context->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    ID3D11ShaderResourceView* views[] = {&shader_resource_view};
    ID3D11SamplerState* samplers[] = {nearest_sampler_state_.Get()};
    gpu_.context->PSSetShaderResources(0, 1, views);
    gpu_.context->PSSetSamplers(0, 1, samplers);
    gpu_.context->Draw(4, 0);
    ID3D11ShaderResourceView* null_views[] = {nullptr};
    gpu_.context->PSSetShaderResources(0, 1, null_views);
    ++last_hotspot_counters_.gpu_draw_call_count;
}

std::optional<FramePixels> DirectXRenderer::read_back_frame() {
    if (presented_software_) {
        return software_->read_back_frame();
    }
    auto* source = use_gpu_scene_ ? scene_texture_.Get() : software_texture_.Get();
    if (source == nullptr || !gpu_.healthy()) {
        return std::nullopt;
    }
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(gpu_.device->CreateTexture2D(&desc, nullptr, &staging))) {
        return std::nullopt;
    }
    gpu_.context->CopyResource(staging.Get(), source);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(gpu_.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return std::nullopt;
    }
    FramePixels result;
    result.width = static_cast<int>(desc.Width);
    result.height = static_cast<int>(desc.Height);
    result.rgba.resize(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
    for (UINT y = 0; y < desc.Height; ++y) {
        const auto* row = static_cast<const uint8_t*>(mapped.pData) +
                          static_cast<std::size_t>(y) * mapped.RowPitch;
        auto* output = result.rgba.data() + static_cast<std::size_t>(y) * desc.Width * 4;
        for (UINT x = 0; x < desc.Width; ++x) {
            output[x * 4] = row[x * 4 + 2];
            output[x * 4 + 1] = row[x * 4 + 1];
            output[x * 4 + 2] = row[x * 4];
            output[x * 4 + 3] = row[x * 4 + 3];
        }
    }
    gpu_.context->Unmap(staging.Get(), 0);
    return result;
}

RenderHotspotCounters DirectXRenderer::last_hotspot_counters() const {
    return last_hotspot_counters_;
}

} // namespace

std::unique_ptr<Renderer> create_d3d11_renderer() {
    return std::make_unique<DirectXRenderer>(RendererBackend::D3D11);
}

std::unique_ptr<Renderer> create_d3d12_renderer() {
    return std::make_unique<DirectXRenderer>(RendererBackend::D3D12);
}

} // namespace nk
