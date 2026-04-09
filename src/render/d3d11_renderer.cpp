#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

namespace nk {

namespace {

using Microsoft::WRL::ComPtr;

constexpr std::size_t kMaxClipDepth = 3;
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
    std::array<ClipRegion, kMaxClipDepth> clips{};
    uint32_t clip_count = 0;
};

struct ImageCommand {
    Rect rect{};
    const uint32_t* pixel_data = nullptr;
    int src_width = 0;
    int src_height = 0;
    ScaleMode scale_mode = ScaleMode::NearestNeighbor;
    std::array<ClipRegion, kMaxClipDepth> clips{};
    uint32_t clip_count = 0;
};

struct TextCommand {
    Rect rect{};
    std::shared_ptr<ShapedText> shaped_text;
    std::array<ClipRegion, kMaxClipDepth> clips{};
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

struct DrawConstants {
    float rect[4]{};
    float color[4]{};
    float clip_rects[12]{};
    float clip_radii[4]{};
    float params0[4]{};
    float viewport[4]{};
};

static_assert(sizeof(DrawConstants) == 128);

struct ImageDrawConstants {
    float rect[4]{};
    float clip_rects[12]{};
    float clip_radii[4]{};
    float viewport[4]{};
};

static_assert(sizeof(ImageDrawConstants) == 96);

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
        hash ^= std::hash<const uint8_t*>{}(key.bitmap_data) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
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
    float4 clip_rects[3];
    float4 clip_radii;
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

constexpr std::string_view kPrimitivePixelShader = R"(
cbuffer DrawConstantsBuffer : register(b0) {
    float4 rect;
    float4 color;
    float4 clip_rects[3];
    float4 clip_radii;
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
    [unroll]
    for (uint clip_index = 0; clip_index < 3; ++clip_index) {
        if (clip_index >= clip_count) {
            break;
        }
        coverage *= saturate(
            0.5 - rounded_rect_sd(pixel_position, clip_rects[clip_index], clip_radii[clip_index]));
    }
    return coverage;
}

float4 main(VertexOutput input) : SV_Target {
    float radius = params0.x;
    float thickness = params0.y;
    uint kind = (uint)params0.z;
    uint clip_count = (uint)params0.w;

    float coverage = saturate(0.5 - rounded_rect_sd(input.pixel, rect, radius));
    coverage *= clip_coverage(input.pixel, clip_count);
    if (kind == 1u) {
        float inner_radius = max(0.0, radius - thickness);
        float4 inner_rect = float4(rect.x + thickness,
                                   rect.y + thickness,
                                   max(0.0, rect.z - (thickness * 2.0)),
                                   max(0.0, rect.w - (thickness * 2.0)));
        coverage = saturate(
            coverage - saturate(0.5 - rounded_rect_sd(input.pixel, inner_rect, inner_radius)));
    }
    return float4(color.rgb, color.a * coverage);
}
)";

constexpr std::string_view kImageVertexShader = R"(
cbuffer ImageDrawConstantsBuffer : register(b0) {
    float4 rect;
    float4 clip_rects[3];
    float4 clip_radii;
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
    float4 clip_rects[3];
    float4 clip_radii;
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
    [unroll]
    for (uint clip_index = 0; clip_index < 3; ++clip_index) {
        if (clip_rects[clip_index].z <= 0.0 || clip_rects[clip_index].w <= 0.0) {
            break;
        }
        coverage *= saturate(
            0.5 - rounded_rect_sd(pixel_position, clip_rects[clip_index], clip_radii[clip_index]));
    }
    return coverage;
}

float4 main(VertexOutput input) : SV_Target {
    float4 sampled = image_texture.Sample(image_sampler, input.uv);
    sampled.a *= clip_coverage(input.pixel);
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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
template <typename T> const IID& type_iid() {
    return __uuidof(T);
}
#pragma clang diagnostic pop

class D3D11Renderer final : public Renderer {
public:
    D3D11Renderer();

    [[nodiscard]] RendererBackend backend() const override;
    bool attach_surface(NativeSurface& surface) override;
    void set_text_shaper(TextShaper* shaper) override;
    void set_damage_regions(std::span<const Rect> regions) override;
    void begin_frame(Size viewport, float scale_factor) override;
    void render(const RenderNode& root) override;
    void end_frame() override;
    void present(NativeSurface& surface) override;
    [[nodiscard]] RenderHotspotCounters last_hotspot_counters() const override;

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
    [[nodiscard]] Rect effective_command_bounds(const ImageCommand& command) const;
    [[nodiscard]] Rect effective_command_bounds(const TextCommand& command) const;
    [[nodiscard]] bool primitive_covers_damage(const PrimitiveCommand& command, Rect damage) const;
    [[nodiscard]] bool damage_requires_clear(Rect damage) const;
    bool command_intersects_damage(const PrimitiveCommand& command, Rect damage) const;
    bool command_intersects_damage(const ImageCommand& command, Rect damage) const;
    bool command_intersects_damage(const TextCommand& command, Rect damage) const;
    void draw_clear_rect(Rect damage);
    void draw_primitive(const PrimitiveCommand& command,
                        std::optional<Rect> scissor,
                        bool already_scaled = false);
    void draw_image(const ImageCommand& command,
                    ID3D11ShaderResourceView& shader_resource_view,
                    std::optional<Rect> scissor);
    void draw_text(const TextCommand& command,
                   ID3D11ShaderResourceView& shader_resource_view,
                   std::optional<Rect> scissor);

    std::unique_ptr<SoftwareRenderer> software_;
    TextShaper* text_shaper_ = nullptr;
    NativeSurface* attached_surface_ = nullptr;
    HWND hwnd_ = nullptr;
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    ComPtr<IDXGISwapChain1> swap_chain_;
    ComPtr<ID3D11Texture2D> back_buffer_;
    ComPtr<ID3D11Texture2D> scene_texture_;
    ComPtr<ID3D11RenderTargetView> scene_render_target_;
    ComPtr<ID3D11Texture2D> software_texture_;
    ComPtr<ID3D11VertexShader> vertex_shader_;
    ComPtr<ID3D11PixelShader> pixel_shader_;
    ComPtr<ID3D11VertexShader> image_vertex_shader_;
    ComPtr<ID3D11PixelShader> image_pixel_shader_;
    ComPtr<ID3D11Buffer> constant_buffer_;
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
    if (!rect.contains(point) && !(point.x == rect.right() && point.y >= rect.y && point.y <= rect.bottom()) &&
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

    constexpr std::size_t kOffsetBasis = 1469598103934665603ULL;
    constexpr std::size_t kPrime = 1099511628211ULL;
    std::size_t hash = kOffsetBasis;
    const auto* bytes = reinterpret_cast<const uint8_t*>(pixel_data);
    const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= static_cast<std::size_t>(bytes[index]);
        hash *= kPrime;
    }
    return hash;
}

D3D11Renderer::D3D11Renderer() : software_(std::make_unique<SoftwareRenderer>()) {}

RendererBackend D3D11Renderer::backend() const {
    return RendererBackend::D3D11;
}

bool D3D11Renderer::attach_surface(NativeSurface& surface) {
    (void)software_->attach_surface(surface);

    auto* hwnd = static_cast<HWND>(surface.native_handle());
    if (hwnd == nullptr) {
        destroy_context();
        attached_surface_ = nullptr;
        return false;
    }

    attached_surface_ = &surface;
    if (hwnd_ == hwnd && device_ != nullptr && swap_chain_ != nullptr) {
        return true;
    }

    hwnd_ = hwnd;
    destroy_device_objects();
    return create_device_objects();
}

void D3D11Renderer::set_text_shaper(TextShaper* shaper) {
    text_shaper_ = shaper;
    software_->set_text_shaper(shaper);
}

void D3D11Renderer::set_damage_regions(std::span<const Rect> regions) {
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
            if (rects_overlap_or_touch(pixel_damage_regions_[outer], pixel_damage_regions_[inner])) {
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

void D3D11Renderer::begin_frame(Size viewport, float scale_factor) {
    logical_viewport_ = viewport;
    scale_factor_ = normalize_scale_factor(scale_factor);
    ++frame_serial_;

    const auto framebuffer =
        attached_surface_ != nullptr ? attached_surface_->framebuffer_size()
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
    image_commands_.clear();
    text_commands_.clear();
    active_clips_.clear();
    last_root_ = nullptr;
    use_gpu_scene_ = false;
    software_frame_rendered_ = false;
    software_frame_finalized_ = false;
    full_present_ = true;
    last_hotspot_counters_ = {};
    last_hotspot_counters_.gpu_viewport_pixel_count =
        static_cast<std::size_t>(framebuffer_width_) * static_cast<std::size_t>(framebuffer_height_);
}

void D3D11Renderer::render(const RenderNode& root) {
    last_root_ = &root;
    draw_commands_.clear();
    primitive_commands_.clear();
    image_commands_.clear();
    text_commands_.clear();
    active_clips_.clear();
    use_gpu_scene_ = collect_gpu_commands(root);

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

void D3D11Renderer::end_frame() {
    if (software_frame_rendered_ && !software_frame_finalized_) {
        software_->end_frame();
        software_frame_finalized_ = true;
    }
}

template <typename Map> void D3D11Renderer::trim_texture_cache(Map& cache, std::size_t max_entries) {
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

bool D3D11Renderer::collect_gpu_commands(const RenderNode& node) {
    switch (node.kind()) {
    case RenderNodeKind::Container:
        for (const auto& child : node.children()) {
            if (child != nullptr && !collect_gpu_commands(*child)) {
                primitive_commands_.clear();
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
        command.radius = effective_corner_radius(rounded_node.bounds(), rounded_node.corner_radius());
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
    case RenderNodeKind::RoundedClip: {
        if (active_clips_.size() >= kMaxClipDepth) {
            primitive_commands_.clear();
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
    default:
        primitive_commands_.clear();
        image_commands_.clear();
        text_commands_.clear();
        draw_commands_.clear();
        return false;
    }
    return false;
}

bool D3D11Renderer::ensure_software_frame() {
    if (software_frame_rendered_) {
        return true;
    }

    software_->begin_frame(logical_viewport_, scale_factor_);
    software_->set_damage_regions(logical_damage_regions_);
    if (last_root_ != nullptr) {
        software_->render(*last_root_);
    }
    software_frame_rendered_ = true;
    last_hotspot_counters_ = software_->last_hotspot_counters();
    return true;
}

std::shared_ptr<ShapedText> D3D11Renderer::shape_text_node(const TextNode& text_node) {
    if (text_shaper_ == nullptr || text_node.text().empty()) {
        return nullptr;
    }

    auto font = renderer_font_for_shape(text_node.font(), scale_factor_);
    const auto color = text_node.text_color();
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
        .scale = scale_factor_,
    };

    if (auto it = shaped_text_cache_.find(key); it != shaped_text_cache_.end()) {
        ++last_hotspot_counters_.text_shape_cache_hit_count;
        return it->second;
    }

    auto shaped = std::make_shared<ShapedText>(text_shaper_->shape(text_node.text(), font, color));
    ++last_hotspot_counters_.text_shape_count;
    shaped_text_cache_[key] = shaped;
    return shaped;
}

bool D3D11Renderer::create_device_objects() {
    destroy_device_objects();

    const D3D_FEATURE_LEVEL requested_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    HRESULT result = D3D11CreateDevice(nullptr,
                                       D3D_DRIVER_TYPE_HARDWARE,
                                       nullptr,
                                       flags,
                                       requested_levels,
                                       static_cast<UINT>(std::size(requested_levels)),
                                       D3D11_SDK_VERSION,
                                       &device_,
                                       &feature_level,
                                       &context_);
    if (result == E_INVALIDARG) {
        const D3D_FEATURE_LEVEL fallback_levels[] = {
            D3D_FEATURE_LEVEL_11_0,
        };
        result = D3D11CreateDevice(nullptr,
                                   D3D_DRIVER_TYPE_HARDWARE,
                                   nullptr,
                                   flags,
                                   fallback_levels,
                                   static_cast<UINT>(std::size(fallback_levels)),
                                   D3D11_SDK_VERSION,
                                   &device_,
                                   &feature_level,
                                   &context_);
    }
    if (FAILED(result)) {
        destroy_device_objects();
        return false;
    }

    ComPtr<IDXGIDevice> dxgi_device;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(device_.As(&dxgi_device)) || FAILED(dxgi_device->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(type_iid<IDXGIFactory2>(),
                                  reinterpret_cast<void**>(factory.GetAddressOf())))) {
        destroy_device_objects();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc{};
    swap_chain_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swap_chain_desc.SampleDesc.Count = 1;
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = 2;
    swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

    if (FAILED(factory->CreateSwapChainForHwnd(
            device_.Get(), hwnd_, &swap_chain_desc, nullptr, nullptr, &swap_chain_))) {
        destroy_device_objects();
        return false;
    }

    (void)factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    return create_pipeline_objects();
}

bool D3D11Renderer::create_pipeline_objects() {
    ComPtr<ID3DBlob> vertex_bytecode;
    ComPtr<ID3DBlob> pixel_bytecode;
    ComPtr<ID3DBlob> image_vertex_bytecode;
    ComPtr<ID3DBlob> image_pixel_bytecode;
    if (FAILED(compile_shader(kPrimitiveVertexShader, "main", "vs_5_0", vertex_bytecode)) ||
        FAILED(compile_shader(kPrimitivePixelShader, "main", "ps_5_0", pixel_bytecode)) ||
        FAILED(compile_shader(kImageVertexShader, "main", "vs_5_0", image_vertex_bytecode)) ||
        FAILED(compile_shader(kImagePixelShader, "main", "ps_5_0", image_pixel_bytecode))) {
        destroy_device_objects();
        return false;
    }

    if (FAILED(device_->CreateVertexShader(vertex_bytecode->GetBufferPointer(),
                                           vertex_bytecode->GetBufferSize(),
                                           nullptr,
                                           &vertex_shader_)) ||
        FAILED(device_->CreatePixelShader(pixel_bytecode->GetBufferPointer(),
                                          pixel_bytecode->GetBufferSize(),
                                          nullptr,
                                          &pixel_shader_)) ||
        FAILED(device_->CreateVertexShader(image_vertex_bytecode->GetBufferPointer(),
                                           image_vertex_bytecode->GetBufferSize(),
                                           nullptr,
                                           &image_vertex_shader_)) ||
        FAILED(device_->CreatePixelShader(image_pixel_bytecode->GetBufferPointer(),
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
    if (FAILED(device_->CreateBuffer(&constant_buffer_desc, nullptr, &constant_buffer_))) {
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
    if (FAILED(device_->CreateBlendState(&blend_desc, &blend_state_))) {
        destroy_device_objects();
        return false;
    }

    D3D11_RASTERIZER_DESC rasterizer_desc{};
    rasterizer_desc.FillMode = D3D11_FILL_SOLID;
    rasterizer_desc.CullMode = D3D11_CULL_NONE;
    rasterizer_desc.ScissorEnable = TRUE;
    rasterizer_desc.DepthClipEnable = TRUE;
    if (FAILED(device_->CreateRasterizerState(&rasterizer_desc, &rasterizer_state_))) {
        destroy_device_objects();
        return false;
    }

    D3D11_SAMPLER_DESC sampler_desc{};
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device_->CreateSamplerState(&sampler_desc, &nearest_sampler_state_))) {
        destroy_device_objects();
        return false;
    }

    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (FAILED(device_->CreateSamplerState(&sampler_desc, &linear_sampler_state_))) {
        destroy_device_objects();
        return false;
    }

    return true;
}

bool D3D11Renderer::ensure_render_targets() {
    if (device_ == nullptr || swap_chain_ == nullptr || attached_surface_ == nullptr) {
        return false;
    }

    const auto framebuffer = attached_surface_->framebuffer_size();
    const int width = std::max(1, static_cast<int>(std::lround(framebuffer.width)));
    const int height = std::max(1, static_cast<int>(std::lround(framebuffer.height)));
    if (width == framebuffer_width_ && height == framebuffer_height_ && back_buffer_ != nullptr &&
        scene_texture_ != nullptr) {
        return true;
    }

    framebuffer_width_ = width;
    framebuffer_height_ = height;

    if (context_ != nullptr) {
        ID3D11RenderTargetView* null_targets[] = {nullptr};
        context_->OMSetRenderTargets(1, null_targets, nullptr);
    }

    scene_render_target_.Reset();
    scene_texture_.Reset();
    back_buffer_.Reset();
    software_texture_.Reset();

    if (FAILED(swap_chain_->ResizeBuffers(
            0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0))) {
        return false;
    }
    if (FAILED(swap_chain_->GetBuffer(0,
                                      type_iid<ID3D11Texture2D>(),
                                      reinterpret_cast<void**>(back_buffer_.GetAddressOf())))) {
        return false;
    }

    D3D11_TEXTURE2D_DESC texture_desc{};
    texture_desc.Width = static_cast<UINT>(width);
    texture_desc.Height = static_cast<UINT>(height);
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(device_->CreateTexture2D(&texture_desc, nullptr, &scene_texture_))) {
        return false;
    }
    if (FAILED(device_->CreateRenderTargetView(scene_texture_.Get(), nullptr, &scene_render_target_))) {
        return false;
    }

    texture_desc.BindFlags = 0;
    if (FAILED(device_->CreateTexture2D(&texture_desc, nullptr, &software_texture_))) {
        return false;
    }

    scene_initialized_ = false;
    return true;
}

void D3D11Renderer::destroy_context() {
    destroy_device_objects();
    hwnd_ = nullptr;
    attached_surface_ = nullptr;
    scene_initialized_ = false;
    framebuffer_width_ = 0;
    framebuffer_height_ = 0;
}

void D3D11Renderer::destroy_device_objects() {
    if (context_ != nullptr) {
        ID3D11RenderTargetView* null_targets[] = {nullptr};
        context_->OMSetRenderTargets(1, null_targets, nullptr);
        context_->ClearState();
        context_->Flush();
    }

    software_texture_.Reset();
    scene_render_target_.Reset();
    scene_texture_.Reset();
    back_buffer_.Reset();
    constant_buffer_.Reset();
    blend_state_.Reset();
    rasterizer_state_.Reset();
    linear_sampler_state_.Reset();
    nearest_sampler_state_.Reset();
    image_pixel_shader_.Reset();
    image_vertex_shader_.Reset();
    pixel_shader_.Reset();
    vertex_shader_.Reset();
    shaped_text_cache_.clear();
    image_texture_cache_.clear();
    text_texture_cache_.clear();
    swap_chain_.Reset();
    context_.Reset();
    device_.Reset();
}

void D3D11Renderer::present(NativeSurface& surface) {
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

ImageTextureCacheEntry* D3D11Renderer::ensure_image_texture(const ImageCommand& command) {
    if (device_ == nullptr || context_ == nullptr || command.pixel_data == nullptr ||
        command.src_width <= 0 || command.src_height <= 0) {
        return nullptr;
    }

    const ImageTextureCacheKey key{
        .pixel_data = command.pixel_data,
        .width = command.src_width,
        .height = command.src_height,
        .content_hash = hash_image_content(command.pixel_data, command.src_width, command.src_height),
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
    if (FAILED(device_->CreateTexture2D(&texture_desc, &initial_data, &entry.texture))) {
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_desc{};
    shader_resource_desc.Format = texture_desc.Format;
    shader_resource_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    shader_resource_desc.Texture2D.MipLevels = 1;
    if (FAILED(device_->CreateShaderResourceView(entry.texture.Get(),
                                                 &shader_resource_desc,
                                                 &entry.shader_resource_view))) {
        return nullptr;
    }

    entry.last_used_frame = frame_serial_;
    ++last_hotspot_counters_.image_texture_upload_count;
    auto [inserted, _] = image_texture_cache_.emplace(key, std::move(entry));
    return &inserted->second;
}

TextTextureCacheEntry* D3D11Renderer::ensure_text_texture(const TextCommand& command) {
    if (device_ == nullptr || context_ == nullptr || command.shaped_text == nullptr ||
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
    if (FAILED(device_->CreateTexture2D(&texture_desc, &initial_data, &entry.texture))) {
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC shader_resource_desc{};
    shader_resource_desc.Format = texture_desc.Format;
    shader_resource_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    shader_resource_desc.Texture2D.MipLevels = 1;
    if (FAILED(device_->CreateShaderResourceView(entry.texture.Get(),
                                                 &shader_resource_desc,
                                                 &entry.shader_resource_view))) {
        return nullptr;
    }

    entry.last_used_frame = frame_serial_;
    ++last_hotspot_counters_.text_texture_upload_count;
    auto [inserted, _] = text_texture_cache_.emplace(key, std::move(entry));
    return &inserted->second;
}

bool D3D11Renderer::draw_gpu_scene() {
    if (!ensure_render_targets() || context_ == nullptr || scene_render_target_ == nullptr) {
        return false;
    }

    const bool full_scene_redraw = full_redraw_ || pixel_damage_regions_.empty() || !scene_initialized_;
    full_present_ = full_scene_redraw;
    const float white[4] = {1.0F, 1.0F, 1.0F, 1.0F};
    ID3D11RenderTargetView* render_targets[] = {scene_render_target_.Get()};
    context_->OMSetRenderTargets(1, render_targets, nullptr);
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    context_->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    context_->RSSetState(rasterizer_state_.Get());
    constexpr float blend_factor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    context_->OMSetBlendState(blend_state_.Get(), blend_factor, 0xFFFFFFFFU);

    D3D11_VIEWPORT viewport{};
    viewport.Width = static_cast<float>(framebuffer_width_);
    viewport.Height = static_cast<float>(framebuffer_height_);
    viewport.MinDepth = 0.0F;
    viewport.MaxDepth = 1.0F;
    context_->RSSetViewports(1, &viewport);

    last_hotspot_counters_.gpu_draw_call_count = 0;
    last_hotspot_counters_.gpu_replayed_command_count = 0;
    last_hotspot_counters_.gpu_skipped_command_count = 0;
    if (full_scene_redraw) {
        context_->ClearRenderTargetView(scene_render_target_.Get(), white);
        last_hotspot_counters_.gpu_replayed_command_count = draw_commands_.size();
        for (const auto& draw_command : draw_commands_) {
            if (draw_command.kind == DrawCommandKind::Primitive) {
                draw_primitive(primitive_commands_[draw_command.command_index], std::nullopt);
                continue;
            }

            if (draw_command.kind == DrawCommandKind::Image) {
                auto* texture = ensure_image_texture(image_commands_[draw_command.command_index]);
                if (texture == nullptr || texture->shader_resource_view == nullptr) {
                    return false;
                }
                draw_image(image_commands_[draw_command.command_index],
                           *texture->shader_resource_view.Get(),
                           std::nullopt);
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

            auto draw_grouped_textured_command = [&](const auto& command, auto ensure_texture, auto draw_fn) {
                const auto bounds =
                    clip_rect_to_viewport(effective_command_bounds(command), framebuffer_width_, framebuffer_height_);
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
                if (union_area <= ((total_area * 5U) / 4U)) {
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

            if (draw_command.kind == DrawCommandKind::Image) {
                const auto& command = image_commands_[draw_command.command_index];
                if (!draw_grouped_textured_command(command,
                                                  [&](const ImageCommand& image_command) {
                                                      return ensure_image_texture(image_command);
                                                  },
                                                  [&](const ImageCommand& image_command,
                                                      ID3D11ShaderResourceView& view,
                                                      Rect scissor) {
                                                      draw_image(image_command, view, scissor);
                                                  })) {
                    return false;
                }
                continue;
            }

            const auto& command = text_commands_[draw_command.command_index];
            const auto bounds =
                clip_rect_to_viewport(effective_command_bounds(command), framebuffer_width_, framebuffer_height_);
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

bool D3D11Renderer::present_scene() {
    if (context_ == nullptr || scene_texture_ == nullptr || back_buffer_ == nullptr) {
        return false;
    }

    const bool full_scene_redraw = full_present_ || pixel_damage_regions_.empty();
    std::vector<RECT> dirty_rects;
    if (!full_scene_redraw) {
        dirty_rects.reserve(pixel_damage_regions_.size());
        for (const auto& region : pixel_damage_regions_) {
            const auto box = to_d3d_box(region);
            dirty_rects.push_back(to_win32_rect(region));
            context_->CopySubresourceRegion(back_buffer_.Get(),
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
        context_->CopyResource(back_buffer_.Get(), scene_texture_.Get());
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

    DXGI_PRESENT_PARAMETERS present_parameters{};
    present_parameters.DirtyRectsCount = static_cast<UINT>(dirty_rects.size());
    present_parameters.pDirtyRects = dirty_rects.data();
    return SUCCEEDED(swap_chain_->Present1(1, 0, &present_parameters));
}

bool D3D11Renderer::present_uploaded_software_frame() {
    if (context_ == nullptr || software_texture_ == nullptr || back_buffer_ == nullptr) {
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

    context_->UpdateSubresource(software_texture_.Get(),
                                0,
                                nullptr,
                                converted_pixels_.data(),
                                static_cast<UINT>(width * 4),
                                0);

    const bool full_scene_redraw = full_redraw_ || pixel_damage_regions_.empty();
    std::vector<RECT> dirty_rects;
    if (!full_scene_redraw) {
        dirty_rects.reserve(pixel_damage_regions_.size());
        for (const auto& region : pixel_damage_regions_) {
            const auto box = to_d3d_box(region);
            dirty_rects.push_back(to_win32_rect(region));
            context_->CopySubresourceRegion(back_buffer_.Get(),
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
        context_->CopyResource(back_buffer_.Get(), software_texture_.Get());
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

    DXGI_PRESENT_PARAMETERS present_parameters{};
    present_parameters.DirtyRectsCount = static_cast<UINT>(dirty_rects.size());
    present_parameters.pDirtyRects = dirty_rects.data();
    return SUCCEEDED(swap_chain_->Present1(1, 0, &present_parameters));
}

void D3D11Renderer::present_software_direct(NativeSurface& surface) {
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

Rect D3D11Renderer::effective_command_bounds(const PrimitiveCommand& command) const {
    Rect bounds = scale_rect(command.rect, scale_factor_);
    for (uint32_t index = 0; index < command.clip_count && index < kMaxClipDepth; ++index) {
        bounds = intersect_rect(bounds, scale_rect(command.clips[index].rect, scale_factor_));
        if (rect_is_empty(bounds)) {
            break;
        }
    }
    return bounds;
}

Rect D3D11Renderer::effective_command_bounds(const ImageCommand& command) const {
    Rect bounds = scale_rect(command.rect, scale_factor_);
    for (uint32_t index = 0; index < command.clip_count && index < kMaxClipDepth; ++index) {
        bounds = intersect_rect(bounds, scale_rect(command.clips[index].rect, scale_factor_));
        if (rect_is_empty(bounds)) {
            break;
        }
    }
    return bounds;
}

Rect D3D11Renderer::effective_command_bounds(const TextCommand& command) const {
    Rect bounds = scale_rect(command.rect, scale_factor_);
    for (uint32_t index = 0; index < command.clip_count && index < kMaxClipDepth; ++index) {
        bounds = intersect_rect(bounds, scale_rect(command.clips[index].rect, scale_factor_));
        if (rect_is_empty(bounds)) {
            break;
        }
    }
    return bounds;
}

bool D3D11Renderer::primitive_covers_damage(const PrimitiveCommand& command, Rect damage) const {
    if (command.kind != 0U || command.color.a < 0.999F || command.clip_count != 0U) {
        return false;
    }

    const auto bounds =
        clip_rect_to_viewport(scale_rect(command.rect, scale_factor_), framebuffer_width_, framebuffer_height_);
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

bool D3D11Renderer::damage_requires_clear(Rect damage) const {
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

bool D3D11Renderer::command_intersects_damage(const PrimitiveCommand& command, Rect damage) const {
    const auto scaled_bounds =
        clip_rect_to_viewport(effective_command_bounds(command), framebuffer_width_, framebuffer_height_);
    return !rect_is_empty(scaled_bounds) && rects_overlap_or_touch(scaled_bounds, damage);
}

bool D3D11Renderer::command_intersects_damage(const ImageCommand& command, Rect damage) const {
    const auto scaled_bounds =
        clip_rect_to_viewport(effective_command_bounds(command), framebuffer_width_, framebuffer_height_);
    return !rect_is_empty(scaled_bounds) && rects_overlap_or_touch(scaled_bounds, damage);
}

bool D3D11Renderer::command_intersects_damage(const TextCommand& command, Rect damage) const {
    const auto scaled_bounds =
        clip_rect_to_viewport(effective_command_bounds(command), framebuffer_width_, framebuffer_height_);
    return !rect_is_empty(scaled_bounds) && rects_overlap_or_touch(scaled_bounds, damage);
}

void D3D11Renderer::draw_clear_rect(Rect damage) {
    PrimitiveCommand clear_command;
    clear_command.rect = damage;
    clear_command.color = Color::from_rgb(255, 255, 255);
    draw_primitive(clear_command, damage, true);
}

void D3D11Renderer::draw_primitive(const PrimitiveCommand& command,
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
        const auto clip_rect = already_scaled ? command.clips[index].rect
                                              : scale_rect(command.clips[index].rect, scale_factor_);
        constants.clip_rects[index * 4 + 0] = clip_rect.x;
        constants.clip_rects[index * 4 + 1] = clip_rect.y;
        constants.clip_rects[index * 4 + 2] = clip_rect.width;
        constants.clip_rects[index * 4 + 3] = clip_rect.height;
        constants.clip_radii[index] = already_scaled ? command.clips[index].radius
                                                     : (command.clips[index].radius * scale_factor_);
    }
    constants.params0[0] = already_scaled ? command.radius : (command.radius * scale_factor_);
    constants.params0[1] = already_scaled ? command.thickness : (command.thickness * scale_factor_);
    constants.params0[2] = static_cast<float>(command.kind);
    constants.params0[3] = static_cast<float>(command.clip_count);
    constants.viewport[0] = static_cast<float>(framebuffer_width_);
    constants.viewport[1] = static_cast<float>(framebuffer_height_);
    context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    context_->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    context_->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);

    const auto scissor_rect = scissor.has_value() ? clip_rect_to_viewport(*scissor,
                                                                          framebuffer_width_,
                                                                          framebuffer_height_)
                                                  : clip_rect_to_viewport(draw_rect,
                                                                          framebuffer_width_,
                                                                          framebuffer_height_);
    const auto d3d_scissor = to_d3d_rect(scissor_rect);
    context_->RSSetScissorRects(1, &d3d_scissor);
    ID3D11ShaderResourceView* null_views[] = {nullptr};
    context_->PSSetShaderResources(0, 1, null_views);
    context_->Draw(4, 0);
    ++last_hotspot_counters_.gpu_draw_call_count;
}

void D3D11Renderer::draw_image(const ImageCommand& command,
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
    context_->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);

    const auto scissor_rect = scissor.has_value() ? clip_rect_to_viewport(*scissor,
                                                                          framebuffer_width_,
                                                                          framebuffer_height_)
                                                  : clip_rect_to_viewport(draw_rect,
                                                                          framebuffer_width_,
                                                                          framebuffer_height_);
    const auto d3d_scissor = to_d3d_rect(scissor_rect);
    context_->RSSetScissorRects(1, &d3d_scissor);
    context_->VSSetShader(image_vertex_shader_.Get(), nullptr, 0);
    context_->PSSetShader(image_pixel_shader_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    context_->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    ID3D11ShaderResourceView* views[] = {&shader_resource_view};
    ID3D11SamplerState* samplers[] = {
        command.scale_mode == ScaleMode::Bilinear ? linear_sampler_state_.Get()
                                                  : nearest_sampler_state_.Get(),
    };
    context_->PSSetShaderResources(0, 1, views);
    context_->PSSetSamplers(0, 1, samplers);
    context_->Draw(4, 0);
    ID3D11ShaderResourceView* null_views[] = {nullptr};
    context_->PSSetShaderResources(0, 1, null_views);
    ++last_hotspot_counters_.gpu_draw_call_count;
}

void D3D11Renderer::draw_text(const TextCommand& command,
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
    context_->UpdateSubresource(constant_buffer_.Get(), 0, nullptr, &constants, 0, 0);

    const auto scissor_rect = scissor.has_value() ? clip_rect_to_viewport(*scissor,
                                                                          framebuffer_width_,
                                                                          framebuffer_height_)
                                                  : clip_rect_to_viewport(draw_rect,
                                                                          framebuffer_width_,
                                                                          framebuffer_height_);
    const auto d3d_scissor = to_d3d_rect(scissor_rect);
    context_->RSSetScissorRects(1, &d3d_scissor);
    context_->VSSetShader(image_vertex_shader_.Get(), nullptr, 0);
    context_->PSSetShader(image_pixel_shader_.Get(), nullptr, 0);
    context_->VSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    context_->PSSetConstantBuffers(0, 1, constant_buffer_.GetAddressOf());
    ID3D11ShaderResourceView* views[] = {&shader_resource_view};
    ID3D11SamplerState* samplers[] = {nearest_sampler_state_.Get()};
    context_->PSSetShaderResources(0, 1, views);
    context_->PSSetSamplers(0, 1, samplers);
    context_->Draw(4, 0);
    ID3D11ShaderResourceView* null_views[] = {nullptr};
    context_->PSSetShaderResources(0, 1, null_views);
    ++last_hotspot_counters_.gpu_draw_call_count;
}

RenderHotspotCounters D3D11Renderer::last_hotspot_counters() const {
    return last_hotspot_counters_;
}

} // namespace

std::unique_ptr<Renderer> create_d3d11_renderer() {
    return std::make_unique<D3D11Renderer>();
}

} // namespace nk
