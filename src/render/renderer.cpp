#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <nk/foundation/types.h>
#include <nk/platform/platform_backend.h>
#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <nk/render/renderer.h>
#include <nk/text/shaped_text.h>
#include <nk/text/text_shaper.h>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include "directx_device.h"

#include <d3d11.h>
#endif

namespace nk {

#if defined(__APPLE__)
std::unique_ptr<Renderer> create_metal_renderer();
#endif

#if defined(_WIN32)
std::unique_ptr<Renderer> create_d3d11_renderer();
std::unique_ptr<Renderer> create_d3d12_renderer();
#endif

#if defined(NK_HAVE_VULKAN) && (defined(__linux__) || defined(_WIN32))
std::unique_ptr<Renderer> create_vulkan_renderer();
#endif

namespace {

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

#if defined(_WIN32)
bool d3d11_hardware_available() {
    static const bool available = [] {
        ID3D11Device* device = nullptr;
        ID3D11DeviceContext* context = nullptr;
        D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
        const D3D_FEATURE_LEVEL requested_levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        HRESULT result = D3D11CreateDevice(nullptr,
                                           D3D_DRIVER_TYPE_HARDWARE,
                                           nullptr,
                                           0,
                                           requested_levels,
                                           static_cast<UINT>(std::size(requested_levels)),
                                           D3D11_SDK_VERSION,
                                           &device,
                                           &feature_level,
                                           &context);
        if (result == E_INVALIDARG) {
            const D3D_FEATURE_LEVEL fallback_levels[] = {
                D3D_FEATURE_LEVEL_11_0,
            };
            result = D3D11CreateDevice(nullptr,
                                       D3D_DRIVER_TYPE_HARDWARE,
                                       nullptr,
                                       0,
                                       fallback_levels,
                                       static_cast<UINT>(std::size(fallback_levels)),
                                       D3D11_SDK_VERSION,
                                       &device,
                                       &feature_level,
                                       &context);
        }

        if (context != nullptr) {
            context->Release();
        }
        if (device != nullptr) {
            device->Release();
        }
        return SUCCEEDED(result);
    }();
    return available;
}
#endif

struct ClipRegion {
    Rect bounds;
    float corner_radius = 0.0F;
};

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

Point scale_point(Point point, float scale_factor) {
    return {
        point.x * scale_factor,
        point.y * scale_factor,
    };
}

int scaled_extent(float logical_extent, float scale_factor) {
    if (!std::isfinite(logical_extent) || logical_extent <= 0.0F) {
        return 0;
    }
    const auto scaled = static_cast<double>(logical_extent) * static_cast<double>(scale_factor);
    if (!std::isfinite(scaled) || scaled > static_cast<double>(std::numeric_limits<int>::max())) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::lround(scaled)));
}

std::size_t scaled_pixel_area(Rect rect, float scale_factor) {
    const auto width = scaled_extent(rect.width, scale_factor);
    const auto height = scaled_extent(rect.height, scale_factor);
    if (width <= 0 || height <= 0) {
        return 0;
    }
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
}

float clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

bool rect_is_empty(Rect rect) {
    return rect.width <= 0.0F || rect.height <= 0.0F;
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

Rect clip_rect_to_viewport(Rect rect, int width, int height) {
    const float x0 = std::clamp(rect.x, 0.0F, static_cast<float>(width));
    const float y0 = std::clamp(rect.y, 0.0F, static_cast<float>(height));
    const float x1 = std::clamp(rect.right(), 0.0F, static_cast<float>(width));
    const float y1 = std::clamp(rect.bottom(), 0.0F, static_cast<float>(height));
    return {x0, y0, std::max(0.0F, x1 - x0), std::max(0.0F, y1 - y0)};
}

float effective_corner_radius(Rect rect, float requested_radius) {
    return std::clamp(requested_radius, 0.0F, std::min(rect.width, rect.height) * 0.5F);
}

float signed_distance_to_rounded_rect(float px, float py, Rect rect, float corner_radius) {
    const float radius = effective_corner_radius(rect, corner_radius);
    const float center_x = rect.x + (rect.width * 0.5F);
    const float center_y = rect.y + (rect.height * 0.5F);
    const float local_x = std::abs(px - center_x);
    const float local_y = std::abs(py - center_y);
    const float half_w = rect.width * 0.5F;
    const float half_h = rect.height * 0.5F;
    const float inner_w = std::max(0.0F, half_w - radius);
    const float inner_h = std::max(0.0F, half_h - radius);
    const float qx = local_x - inner_w;
    const float qy = local_y - inner_h;

    // Fast-path: strictly inside the rectangular core
    if (qx <= 0.0F && qy <= 0.0F) {
        return std::max(qx, qy) - radius;
    }

    const float outside_x = std::max(qx, 0.0F);
    const float outside_y = std::max(qy, 0.0F);
    const float outside = std::sqrt((outside_x * outside_x) + (outside_y * outside_y));
    const float inside = std::min(std::max(qx, qy), 0.0F);
    return outside + inside - radius;
}

float rounded_rect_coverage(float px, float py, Rect rect, float corner_radius) {
    return clamp01(0.5F - signed_distance_to_rounded_rect(px, py, rect, corner_radius));
}

float clip_coverage(int x, int y, const std::vector<ClipRegion>& clips) {
    float coverage = 1.0F;
    const float sample_x = static_cast<float>(x) + 0.5F;
    const float sample_y = static_cast<float>(y) + 0.5F;
    for (const auto& clip : clips) {
        coverage = std::min(
            coverage, rounded_rect_coverage(sample_x, sample_y, clip.bounds, clip.corner_radius));
        if (coverage <= 0.0F) {
            return 0.0F;
        }
    }
    return coverage;
}

// Intersect a pixel span with the fully covered interior of a rounded rect.
// Keep a small margin beyond the antialias ramp so edge pixels still use the
// scalar coverage calculation, including at fractional device coordinates.
void intersect_opaque_span(Rect rect, float corner_radius, int y, int& x0, int& x1) {
    constexpr float kOpaqueInset = 0.501F;
    if (x0 >= x1) {
        return;
    }
    const float sample_y = static_cast<float>(y) + 0.5F;
    if (sample_y < rect.y + kOpaqueInset || sample_y > rect.bottom() - kOpaqueInset) {
        x1 = x0;
        return;
    }
    const float radius = effective_corner_radius(rect, corner_radius);
    float horizontal_inset = kOpaqueInset;
    if (radius > kOpaqueInset) {
        const float dy = std::max(0.0F,
                                  std::abs(sample_y - (rect.y + (rect.height * 0.5F))) -
                                      (rect.height * 0.5F) + radius);
        const float inner_radius = radius - kOpaqueInset;
        if (dy > inner_radius) {
            x1 = x0;
            return;
        }
        horizontal_inset = radius - std::sqrt((inner_radius * inner_radius) - (dy * dy));
    }
    const float left = std::max(rect.x + horizontal_inset - 0.5F, static_cast<float>(x0));
    const float right =
        std::min(rect.right() - horizontal_inset - 0.5F, static_cast<float>(x1 - 1));
    if (left > right) {
        x1 = x0;
        return;
    }
    x0 = static_cast<int>(std::ceil(left));
    x1 = static_cast<int>(std::floor(right)) + 1;
    x1 = std::max(x0, x1);
}

std::array<uint8_t, 4> opaque_pixel(Color color) {
    return {static_cast<uint8_t>(std::clamp(color.r * 255.0F, 0.0F, 255.0F)),
            static_cast<uint8_t>(std::clamp(color.g * 255.0F, 0.0F, 255.0F)),
            static_cast<uint8_t>(std::clamp(color.b * 255.0F, 0.0F, 255.0F)),
            255};
}

void fill_opaque_span(std::vector<uint8_t>& pixels,
                      int width,
                      int y,
                      int x0,
                      int x1,
                      const std::array<uint8_t, 4>& pixel) {
    auto* dest = pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                  static_cast<std::size_t>(x0)) *
                                     4U;
    for (int x = x0; x < x1; ++x) {
        std::memcpy(dest, pixel.data(), pixel.size());
        dest += pixel.size();
    }
}

void blend_pixel(std::vector<uint8_t>& pixels,
                 int pixel_width,
                 int x,
                 int y,
                 Color color,
                 float coverage = 1.0F) {
    const float alpha = clamp01(color.a * coverage);
    if (alpha <= 0.0F) {
        return;
    }

    const auto idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(pixel_width) +
                      static_cast<std::size_t>(x)) *
                     4U;

    // Fast-path: opaque pixel overwrite
    if (alpha >= 0.999F) {
        pixels[idx + 0] = static_cast<uint8_t>(std::clamp(color.r * 255.0F, 0.0F, 255.0F));
        pixels[idx + 1] = static_cast<uint8_t>(std::clamp(color.g * 255.0F, 0.0F, 255.0F));
        pixels[idx + 2] = static_cast<uint8_t>(std::clamp(color.b * 255.0F, 0.0F, 255.0F));
        pixels[idx + 3] = 255;
        return;
    }

    const float inv_a = 1.0F - alpha;
    const float destination_alpha = pixels[idx + 3] / 255.0F;
    const float output_alpha = alpha + destination_alpha * inv_a;
    const std::array<float, 3> channels{color.r, color.g, color.b};
    for (std::size_t channel = 0; channel < channels.size(); ++channel) {
        pixels[idx + channel] = static_cast<uint8_t>(
            std::clamp(((channels[channel] * 255.0F * alpha) +
                        static_cast<float>(pixels[idx + channel]) * destination_alpha * inv_a) /
                           output_alpha,
                       0.0F,
                       255.0F));
    }
    pixels[idx + 3] = static_cast<uint8_t>(std::clamp(output_alpha * 255.0F, 0.0F, 255.0F));
}

Point transform_point(Point p, const Matrix3x2& m) {
    return {p.x * m.m11 + p.y * m.m21 + m.m31, p.x * m.m12 + p.y * m.m22 + m.m32};
}

Rect transform_bounds(Rect r, const Matrix3x2& m) {
    const std::array<Point, 4> points{{transform_point({r.x, r.y}, m),
                                       transform_point({r.right(), r.y}, m),
                                       transform_point({r.x, r.bottom()}, m),
                                       transform_point({r.right(), r.bottom()}, m)}};
    float left = points[0].x, right = left, top = points[0].y, bottom = top;
    for (auto p : points) {
        left = std::min(left, p.x);
        right = std::max(right, p.x);
        top = std::min(top, p.y);
        bottom = std::max(bottom, p.y);
    }
    return {left, top, right - left, bottom - top};
}

Rect subtree_bounds(const RenderNode& node) {
    Rect result = node.bounds();
    for (const auto& child : node.children()) {
        if (!child) {
            continue;
        }
        const auto b = subtree_bounds(*child);
        if (rect_is_empty(b)) {
            continue;
        }
        if (rect_is_empty(result)) {
            result = b;
        } else {
            const auto right = std::max(result.right(), b.right());
            const auto bottom = std::max(result.bottom(), b.bottom());
            result.x = std::min(result.x, b.x);
            result.y = std::min(result.y, b.y);
            result.width = right - result.x;
            result.height = bottom - result.y;
        }
    }
    return node.kind() == RenderNodeKind::Transform
               ? transform_bounds(result, static_cast<const TransformNode&>(node).transform())
               : result;
}

struct TextKey {
    std::string text;
    std::string family;
    float size;
    int weight;
    int style;
    float r, g, b, a;
    float max_width;
    float scale;

    bool operator==(const TextKey& o) const {
        return text == o.text && family == o.family && size == o.size && weight == o.weight &&
               style == o.style && r == o.r && g == o.g && b == o.b && a == o.a &&
               max_width == o.max_width && scale == o.scale;
    }
};

struct TextKeyHash {
    std::size_t operator()(const TextKey& k) const {
        std::size_t h = std::hash<std::string>{}(k.text);
        h ^= std::hash<std::string>{}(k.family) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(k.size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.weight) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.style) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(k.r) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(k.g) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(k.b) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(k.a) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(k.max_width) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(k.scale) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

std::string_view renderer_backend_name(RendererBackend backend) noexcept {
    switch (backend) {
    case RendererBackend::Software:
        return "software";
    case RendererBackend::D3D11:
        return "d3d11";
    case RendererBackend::D3D12:
        return "d3d12";
    case RendererBackend::Metal:
        return "metal";
    case RendererBackend::OpenGL:
        return "opengl";
    case RendererBackend::Vulkan:
        return "vulkan";
    }
    return "unknown";
}

bool renderer_backend_is_gpu(RendererBackend backend) noexcept {
    return backend != RendererBackend::Software;
}

bool renderer_backend_supported(RendererBackendSupport support, RendererBackend backend) noexcept {
    switch (backend) {
    case RendererBackend::Software:
        return support.software;
    case RendererBackend::D3D11:
        return support.d3d11;
    case RendererBackend::D3D12:
        return support.d3d12;
    case RendererBackend::Metal:
        return support.metal;
    case RendererBackend::OpenGL:
        return support.open_gl;
    case RendererBackend::Vulkan:
        return support.vulkan;
    }
    return false;
}

bool renderer_backend_available(RendererBackend backend) noexcept {
    switch (backend) {
    case RendererBackend::Software:
        return true;
    case RendererBackend::D3D11:
#if defined(_WIN32)
        return d3d11_hardware_available();
#else
        return false;
#endif
    case RendererBackend::D3D12:
#if defined(_WIN32)
        return directx12_available();
#else
        return false;
#endif
    case RendererBackend::Metal:
#if defined(__APPLE__)
        return true;
#else
        return false;
#endif
    case RendererBackend::OpenGL:
        return false;
    case RendererBackend::Vulkan:
#if defined(NK_HAVE_VULKAN) && (defined(__linux__) || defined(_WIN32))
        return true;
#else
        return false;
#endif
    }
    return false;
}

bool Renderer::attach_surface(NativeSurface& surface) {
    (void)surface;
    return true;
}

void Renderer::set_text_shaper(TextShaper* shaper) {
    (void)shaper;
}

void Renderer::set_damage_regions(std::span<const Rect> regions) {
    (void)regions;
}

RenderHotspotCounters Renderer::last_hotspot_counters() const {
    return {};
}

std::optional<FramePixels> Renderer::read_back_frame() {
    return std::nullopt;
}

// --- SoftwareRenderer ---

struct SoftwareRenderer::Impl {
    Size logical_viewport;
    float scale_factor = 1.0F;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA8
    std::vector<Rect> damage_regions;
    TextShaper* text_shaper = nullptr;
    std::unordered_map<TextKey, std::shared_ptr<ShapedText>, TextKeyHash> shaped_text_cache;
    RenderHotspotCounters last_hotspot_counters;
    bool full_redraw = true;
    bool force_full_redraw = true;
    // Used only by bounded affine fallback layers. Coordinates remain logical;
    // the origin shifts negative source coordinates into the layer's pixel buffer.
    Point render_origin{};
    const RenderNode* ignored_transform = nullptr;
    bool transparent_background = false;
    float initial_opacity = 1.0F;
};

SoftwareRenderer::SoftwareRenderer() : impl_(std::make_unique<Impl>()) {}

SoftwareRenderer::~SoftwareRenderer() = default;

RendererBackend SoftwareRenderer::backend() const {
    return RendererBackend::Software;
}

bool SoftwareRenderer::attach_surface(NativeSurface& surface) {
    (void)surface;
    return true;
}

void SoftwareRenderer::begin_frame(Size viewport, float scale_factor) {
    constexpr std::size_t kMaxFramebufferBytes = 512U * 1024U * 1024U;

    impl_->logical_viewport = viewport;
    impl_->scale_factor = normalize_scale_factor(scale_factor);
    if (impl_->text_shaper != nullptr) {
        impl_->text_shaper->set_scale_factor(impl_->scale_factor);
    }
    int next_width = scaled_extent(viewport.width, impl_->scale_factor);
    int next_height = scaled_extent(viewport.height, impl_->scale_factor);
    std::size_t size = 0;
    if (next_width > 0 && next_height > 0) {
        const auto w = static_cast<std::size_t>(next_width);
        const auto h = static_cast<std::size_t>(next_height);
        if (w > kMaxFramebufferBytes / 4U || h > kMaxFramebufferBytes / (w * 4U)) {
            next_width = 0;
            next_height = 0;
        } else {
            size = w * h * 4U;
        }
    }

    const bool size_changed = next_width != impl_->width || next_height != impl_->height;
    impl_->width = next_width;
    impl_->height = next_height;
    impl_->last_hotspot_counters = {};
    impl_->damage_regions.clear();
    impl_->full_redraw = true;
    impl_->force_full_redraw = size_changed || impl_->pixels.empty();
    impl_->last_hotspot_counters.gpu_viewport_pixel_count =
        static_cast<std::size_t>(std::max(0, impl_->width)) *
        static_cast<std::size_t>(std::max(0, impl_->height));

    if (size_changed || impl_->pixels.size() != size) {
        impl_->pixels.resize(size);
        if (!impl_->pixels.empty()) {
            std::memset(impl_->pixels.data(), 0xFF, impl_->pixels.size());
        }
    }
}

void SoftwareRenderer::set_damage_regions(std::span<const Rect> regions) {
    impl_->damage_regions.clear();
    if (impl_->width <= 0 || impl_->height <= 0 || regions.empty()) {
        impl_->last_hotspot_counters.damage_region_count = 0;
        impl_->full_redraw = true;
        return;
    }

    impl_->damage_regions.reserve(regions.size());
    for (const auto& region : regions) {
        const auto scaled = clip_rect_to_viewport(
            scale_rect(region, impl_->scale_factor), impl_->width, impl_->height);
        if (rect_is_empty(scaled)) {
            continue;
        }

        bool merged_existing = false;
        for (auto& existing : impl_->damage_regions) {
            if (rects_overlap_or_touch(existing, scaled)) {
                existing = union_rect(existing, scaled);
                merged_existing = true;
                break;
            }
        }

        if (!merged_existing) {
            impl_->damage_regions.push_back(scaled);
        }
    }

    for (std::size_t outer = 0; outer < impl_->damage_regions.size(); ++outer) {
        for (std::size_t inner = outer + 1; inner < impl_->damage_regions.size();) {
            if (rects_overlap_or_touch(impl_->damage_regions[outer],
                                       impl_->damage_regions[inner])) {
                impl_->damage_regions[outer] =
                    union_rect(impl_->damage_regions[outer], impl_->damage_regions[inner]);
                impl_->damage_regions.erase(impl_->damage_regions.begin() +
                                            static_cast<std::ptrdiff_t>(inner));
            } else {
                ++inner;
            }
        }
    }

    impl_->last_hotspot_counters.damage_region_count = impl_->damage_regions.size();
    impl_->full_redraw = impl_->force_full_redraw || impl_->damage_regions.empty();
}

void SoftwareRenderer::render(const RenderNode& root) {
    auto device_rect = [&](Rect rect) {
        auto result = scale_rect(rect, impl_->scale_factor);
        result.x += impl_->render_origin.x;
        result.y += impl_->render_origin.y;
        return result;
    };
    auto device_point = [&](Point point) {
        auto result = scale_point(point, impl_->scale_factor);
        result.x += impl_->render_origin.x;
        result.y += impl_->render_origin.y;
        return result;
    };
    auto clear_rect = [&](Rect rect) {
        const int x0 = std::max(0, static_cast<int>(std::floor(rect.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(rect.y)));
        const int x1 = std::min(impl_->width, static_cast<int>(std::ceil(rect.right())));
        const int y1 = std::min(impl_->height, static_cast<int>(std::ceil(rect.bottom())));
        if (x1 <= x0 || y1 <= y0) {
            return;
        }

        for (int y = y0; y < y1; ++y) {
            auto* row = impl_->pixels.data() +
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(impl_->width) +
                         static_cast<std::size_t>(x0)) *
                            4U;
            std::memset(row,
                        impl_->transparent_background ? 0 : 0xFF,
                        static_cast<std::size_t>(x1 - x0) * 4);
        }
    };

    if (impl_->full_redraw || impl_->damage_regions.empty()) {
        if (!impl_->pixels.empty()) {
            std::memset(impl_->pixels.data(),
                        impl_->transparent_background ? 0 : 0xFF,
                        impl_->pixels.size());
        }
        impl_->last_hotspot_counters.damage_region_count = 0;
        impl_->last_hotspot_counters.gpu_estimated_draw_pixel_count =
            impl_->last_hotspot_counters.gpu_viewport_pixel_count;
    } else {
        std::size_t total_damage_pixels = 0;
        for (const auto& damage : impl_->damage_regions) {
            clear_rect(damage);
            total_damage_pixels += scaled_pixel_area(damage, 1.0F);
        }
        impl_->last_hotspot_counters.gpu_estimated_draw_pixel_count = total_damage_pixels;
    }

    auto render_node = [&](auto&& self,
                           const RenderNode& node,
                           std::vector<ClipRegion>& clips,
                           int c_x0,
                           int c_y0,
                           int c_x1,
                           int c_y1,
                           float opacity = 1.0F) -> void {
        if (c_x1 <= c_x0 || c_y1 <= c_y0) {
            return;
        }

        if (node.kind() == RenderNodeKind::Transform && &node != impl_->ignored_transform) {
            const auto& matrix = static_cast<const TransformNode&>(node).transform();
            const float determinant = matrix.m11 * matrix.m22 - matrix.m12 * matrix.m21;
            const std::array<float, 6> values{
                matrix.m11, matrix.m12, matrix.m21, matrix.m22, matrix.m31, matrix.m32};
            if (!std::all_of(
                    values.begin(), values.end(), [](float v) { return std::isfinite(v); }) ||
                !std::isfinite(determinant) || determinant == 0.0F) {
                return;
            }
            // Find source bounds before applying this transform, including
            // content outside the window that the transform brings into view.
            Rect source_bounds{};
            for (const auto& child : node.children()) {
                if (!child) {
                    continue;
                }
                const auto b = subtree_bounds(*child);
                if (rect_is_empty(b)) {
                    continue;
                }
                if (rect_is_empty(source_bounds)) {
                    source_bounds = b;
                } else {
                    const float right = std::max(source_bounds.right(), b.right());
                    const float bottom = std::max(source_bounds.bottom(), b.bottom());
                    source_bounds.x = std::min(source_bounds.x, b.x);
                    source_bounds.y = std::min(source_bounds.y, b.y);
                    source_bounds.width = right - source_bounds.x;
                    source_bounds.height = bottom - source_bounds.y;
                }
            }
            if (rect_is_empty(source_bounds)) {
                return;
            }
            const float sf = impl_->scale_factor;
            const float left = std::floor(source_bounds.x * sf) - 1;
            const float top = std::floor(source_bounds.y * sf) - 1;
            const float width = std::ceil(source_bounds.right() * sf) - left + 1;
            const float height = std::ceil(source_bounds.bottom() * sf) - top + 1;
            SoftwareRenderer layer;
            layer.impl_->transparent_background = true;
            layer.impl_->render_origin = {-left, -top};
            layer.impl_->ignored_transform = &node;
            layer.impl_->initial_opacity = opacity;
            layer.set_text_shaper(impl_->text_shaper);
            layer.begin_frame({width / sf, height / sf}, sf);
            if (layer.pixel_width() <= 0 || layer.pixel_height() <= 0) {
                return;
            }
            layer.render(node);
            const auto bounds = device_rect(transform_bounds(source_bounds, matrix));
            if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
                !std::isfinite(bounds.right()) || !std::isfinite(bounds.bottom())) {
                return;
            }
            const int x0 = static_cast<int>(std::clamp(
                std::floor(bounds.x), static_cast<float>(c_x0), static_cast<float>(c_x1)));
            const int y0 = static_cast<int>(std::clamp(
                std::floor(bounds.y), static_cast<float>(c_y0), static_cast<float>(c_y1)));
            const int x1 = static_cast<int>(std::clamp(
                std::ceil(bounds.right()), static_cast<float>(c_x0), static_cast<float>(c_x1)));
            const int y1 = static_cast<int>(std::clamp(
                std::ceil(bounds.bottom()), static_cast<float>(c_y0), static_cast<float>(c_y1)));
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const float tx =
                        (static_cast<float>(x) + 0.5F - impl_->render_origin.x) / sf - matrix.m31;
                    const float ty =
                        (static_cast<float>(y) + 0.5F - impl_->render_origin.y) / sf - matrix.m32;
                    const float sx =
                        ((tx * matrix.m22 - ty * matrix.m21) / determinant) * sf - left;
                    const float sy = ((ty * matrix.m11 - tx * matrix.m12) / determinant) * sf - top;
                    if (!(sx >= 0 && sy >= 0 && sx < layer.pixel_width() &&
                          sy < layer.pixel_height())) {
                        continue;
                    }
                    const auto index = (static_cast<std::size_t>(sy) * layer.pixel_width() +
                                        static_cast<std::size_t>(sx)) *
                                       4;
                    const auto* pixels = layer.pixel_data();
                    blend_pixel(impl_->pixels,
                                impl_->width,
                                x,
                                y,
                                {pixels[index] / 255.0F,
                                 pixels[index + 1] / 255.0F,
                                 pixels[index + 2] / 255.0F,
                                 pixels[index + 3] / 255.0F},
                                clip_coverage(x, y, clips));
                }
            }
            return;
        }

        if (node.kind() != RenderNodeKind::Text) {
            const auto scaled_bounds = device_rect(node.bounds());
            if (!rect_is_empty(scaled_bounds)) {
                const int node_x0 = static_cast<int>(std::floor(scaled_bounds.x));
                const int node_y0 = static_cast<int>(std::floor(scaled_bounds.y));
                const int node_x1 = static_cast<int>(std::ceil(scaled_bounds.right()));
                const int node_y1 = static_cast<int>(std::ceil(scaled_bounds.bottom()));
                c_x0 = std::max(c_x0, node_x0);
                c_y0 = std::max(c_y0, node_y0);
                c_x1 = std::min(c_x1, node_x1);
                c_y1 = std::min(c_y1, node_y1);
                if (c_x1 <= c_x0 || c_y1 <= c_y0) {
                    return;
                }
            }
        }

        if (node.kind() == RenderNodeKind::RoundedClip) {
            const auto& clip_node = static_cast<const RoundedClipNode&>(node);
            const auto scaled_bounds = device_rect(clip_node.bounds());
            clips.push_back({.bounds = scaled_bounds,
                             .corner_radius = clip_node.corner_radius() * impl_->scale_factor});
            int next_x0 = std::max(c_x0, static_cast<int>(std::floor(scaled_bounds.x)));
            int next_y0 = std::max(c_y0, static_cast<int>(std::floor(scaled_bounds.y)));
            int next_x1 = std::min(c_x1, static_cast<int>(std::ceil(scaled_bounds.right())));
            int next_y1 = std::min(c_y1, static_cast<int>(std::ceil(scaled_bounds.bottom())));

            for (const auto& child : node.children()) {
                self(self, *child, clips, next_x0, next_y0, next_x1, next_y1, opacity);
            }
            clips.pop_back();
            return;
        }

        if (node.kind() == RenderNodeKind::Opacity) {
            const auto& opacity_node = static_cast<const OpacityNode&>(node);
            const float child_opacity = opacity * opacity_node.opacity();
            for (const auto& child : node.children()) {
                self(self, *child, clips, c_x0, c_y0, c_x1, c_y1, child_opacity);
            }
            return;
        }

        if (node.kind() == RenderNodeKind::LinearGradient) {
            const auto& grad = static_cast<const LinearGradientNode&>(node);
            const auto b = device_rect(grad.bounds());
            const auto sc = grad.start_color();
            const auto ec = grad.end_color();
            const bool vertical = (grad.direction() == Orientation::Vertical);
            const float span = vertical ? b.height : b.width;

            const int x0 = std::max(c_x0, static_cast<int>(std::floor(b.x)));
            const int y0 = std::max(c_y0, static_cast<int>(std::floor(b.y)));
            const int x1 = std::min(c_x1, static_cast<int>(std::ceil(b.right())));
            const int y1 = std::min(c_y1, static_cast<int>(std::ceil(b.bottom())));

            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const float pos = vertical ? (static_cast<float>(y) + 0.5F - b.y)
                                               : (static_cast<float>(x) + 0.5F - b.x);
                    const float t = (span > 0.0F) ? clamp01(pos / span) : 0.0F;
                    Color c{
                        sc.r + (ec.r - sc.r) * t,
                        sc.g + (ec.g - sc.g) * t,
                        sc.b + (ec.b - sc.b) * t,
                        (sc.a + (ec.a - sc.a) * t) * opacity,
                    };
                    const float coverage = clip_coverage(x, y, clips);
                    if (coverage > 0.0F) {
                        blend_pixel(impl_->pixels, impl_->width, x, y, c, coverage);
                    }
                }
            }
        }

        if (node.kind() == RenderNodeKind::Path) {
            const auto& path = static_cast<const PathNode&>(node);
            const auto& points = path.path().points;
            if (points.empty()) {
                return;
            }
            auto color = path.stroke_color();
            color.a *= opacity;
            const float radius = std::max(0.5F, path.thickness() * impl_->scale_factor * 0.5F);
            const std::size_t segments =
                points.size() == 1 ? 1 : points.size() - 1 + (path.path().closed ? 1 : 0);
            for (int y = c_y0; y < c_y1; ++y) {
                for (int x = c_x0; x < c_x1; ++x) {
                    float distance = std::numeric_limits<float>::max();
                    for (std::size_t i = 0; i < segments; ++i) {
                        const auto a = device_point(points[i % points.size()]);
                        const auto b = device_point(points[(i + 1) % points.size()]);
                        const float dx = b.x - a.x, dy = b.y - a.y;
                        const float length_squared = dx * dx + dy * dy;
                        const float t =
                            length_squared > 0
                                ? std::clamp(((static_cast<float>(x) + 0.5F - a.x) * dx +
                                              (static_cast<float>(y) + 0.5F - a.y) * dy) /
                                                 length_squared,
                                             0.0F,
                                             1.0F)
                                : 0.0F;
                        distance =
                            std::min(distance,
                                     std::hypot(static_cast<float>(x) + 0.5F - a.x - t * dx,
                                                static_cast<float>(y) + 0.5F - a.y - t * dy));
                    }
                    blend_pixel(impl_->pixels,
                                impl_->width,
                                x,
                                y,
                                color,
                                clamp01(radius + 0.5F - distance) * clip_coverage(x, y, clips));
                }
            }
        }

        if (node.kind() == RenderNodeKind::Line) {
            const auto& line = static_cast<const LineNode&>(node);
            const auto start = device_point(line.start());
            const auto end = device_point(line.end());
            const float radius = std::max(0.5F, line.thickness() * impl_->scale_factor * 0.5F);
            const float delta_x = end.x - start.x;
            const float delta_y = end.y - start.y;
            const float length_squared = (delta_x * delta_x) + (delta_y * delta_y);
            auto color = line.color();
            color.a *= opacity;

            for (int y = c_y0; y < c_y1; ++y) {
                for (int x = c_x0; x < c_x1; ++x) {
                    const float pixel_x = static_cast<float>(x) + 0.5F;
                    const float pixel_y = static_cast<float>(y) + 0.5F;
                    const float projection = length_squared > 0.0F
                                                 ? std::clamp(((pixel_x - start.x) * delta_x +
                                                               (pixel_y - start.y) * delta_y) /
                                                                  length_squared,
                                                              0.0F,
                                                              1.0F)
                                                 : 0.0F;
                    const float nearest_x = start.x + (delta_x * projection);
                    const float nearest_y = start.y + (delta_y * projection);
                    const float distance = std::hypot(pixel_x - nearest_x, pixel_y - nearest_y);
                    const float coverage =
                        clamp01(radius + 0.5F - distance) * clip_coverage(x, y, clips);
                    if (coverage > 0.0F) {
                        blend_pixel(impl_->pixels, impl_->width, x, y, color, coverage);
                    }
                }
            }
        }

        if (node.kind() == RenderNodeKind::Shadow) {
            const auto& shadow = static_cast<const ShadowNode&>(node);
            const float sf = impl_->scale_factor;
            const float blur = shadow.blur_radius() * sf;
            const float cr = shadow.corner_radius() * sf;
            // The shadow rect is the element rect shifted by offset and grown by spread.
            // We need the original element rect from the node's stored bounds (which
            // were expanded by blur+spread in the constructor).
            const auto full = scale_rect(shadow.bounds(), sf);
            const Rect shadow_rect{full.x + blur,
                                   full.y + blur,
                                   std::max(0.0F, full.width - blur * 2.0F),
                                   std::max(0.0F, full.height - blur * 2.0F)};
            auto sc = shadow.color();
            sc.a *= opacity;

            const int x0 = std::max(c_x0, static_cast<int>(std::floor(full.x)));
            const int y0 = std::max(c_y0, static_cast<int>(std::floor(full.y)));
            const int x1 = std::min(c_x1, static_cast<int>(std::ceil(full.right())));
            const int y1 = std::min(c_y1, static_cast<int>(std::ceil(full.bottom())));

            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const float px = static_cast<float>(x) + 0.5F;
                    const float py = static_cast<float>(y) + 0.5F;
                    const float dist = signed_distance_to_rounded_rect(px, py, shadow_rect, cr);
                    if (dist >= blur) {
                        continue;
                    }
                    // Smooth falloff: full alpha inside, linear fade to zero at blur edge.
                    const float shadow_alpha = blur > 0.0F
                                                   ? clamp01(1.0F - std::max(0.0F, dist) / blur)
                                                   : (dist < 0.0F ? 1.0F : 0.0F);
                    const float coverage = clip_coverage(x, y, clips);
                    if (coverage > 0.0F && shadow_alpha > 0.0F) {
                        Color pixel_color{sc.r, sc.g, sc.b, sc.a * shadow_alpha};
                        blend_pixel(impl_->pixels, impl_->width, x, y, pixel_color, coverage);
                    }
                }
            }
        }

        if (node.kind() == RenderNodeKind::ColorRect) {
            const auto& color_node = static_cast<const ColorRectNode&>(node);
            const auto b = device_rect(color_node.bounds());
            auto c = color_node.color();
            c.a *= opacity;

            const int x0 = std::max(c_x0, static_cast<int>(std::floor(b.x)));
            const int y0 = std::max(c_y0, static_cast<int>(std::floor(b.y)));
            const int x1 = std::min(c_x1, static_cast<int>(std::ceil(b.right())));
            const int y1 = std::min(c_y1, static_cast<int>(std::ceil(b.bottom())));

            const auto pixel = c.a >= 0.999F ? opaque_pixel(c) : std::array<uint8_t, 4>{};
            for (int y = y0; y < y1 && c.a > 0.0F; ++y) {
                int fill_x0 = x0;
                int fill_x1 = c.a >= 0.999F ? x1 : x0;
                for (const auto& clip : clips) {
                    intersect_opaque_span(clip.bounds, clip.corner_radius, y, fill_x0, fill_x1);
                }
                for (int x = x0; x < x1; ++x) {
                    if (x == fill_x0 && fill_x1 > fill_x0) {
                        fill_opaque_span(impl_->pixels, impl_->width, y, fill_x0, fill_x1, pixel);
                        x = fill_x1 - 1;
                        continue;
                    }
                    const float coverage = clip_coverage(x, y, clips);
                    if (coverage > 0.0F) {
                        blend_pixel(impl_->pixels, impl_->width, x, y, c, coverage);
                    }
                }
            }
        }

        if (node.kind() == RenderNodeKind::RoundedRect) {
            const auto& rounded_node = static_cast<const RoundedRectNode&>(node);
            const auto b = device_rect(rounded_node.bounds());
            auto c = rounded_node.color();
            c.a *= opacity;
            const float radius = rounded_node.corner_radius() * impl_->scale_factor;

            const int x0 = std::max(c_x0, static_cast<int>(std::floor(b.x)));
            const int y0 = std::max(c_y0, static_cast<int>(std::floor(b.y)));
            const int x1 = std::min(c_x1, static_cast<int>(std::ceil(b.right())));
            const int y1 = std::min(c_y1, static_cast<int>(std::ceil(b.bottom())));

            const auto pixel = c.a >= 0.999F ? opaque_pixel(c) : std::array<uint8_t, 4>{};

            for (int y = y0; y < y1 && c.a > 0.0F; ++y) {
                int fill_x0 = x0;
                int fill_x1 = x1;
                if (c.a >= 0.999F) {
                    intersect_opaque_span(b, radius, y, fill_x0, fill_x1);
                    for (const auto& clip : clips) {
                        intersect_opaque_span(clip.bounds, clip.corner_radius, y, fill_x0, fill_x1);
                    }
                } else {
                    fill_x1 = fill_x0;
                }
                for (int x = x0; x < x1; ++x) {
                    if (x == fill_x0 && fill_x1 > fill_x0) {
                        fill_opaque_span(impl_->pixels, impl_->width, y, fill_x0, fill_x1, pixel);
                        x = fill_x1 - 1;
                        continue;
                    }
                    const float coverage = rounded_rect_coverage(
                        static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F, b, radius);
                    const float clipped = coverage * clip_coverage(x, y, clips);
                    if (clipped > 0.0F) {
                        blend_pixel(impl_->pixels, impl_->width, x, y, c, clipped);
                    }
                }
            }
        }

        if (node.kind() == RenderNodeKind::Border) {
            const auto& border_node = static_cast<const BorderNode&>(node);
            const auto b = device_rect(border_node.bounds());
            auto c = border_node.color();
            c.a *= opacity;
            const float thickness = std::max(0.0F, border_node.thickness() * impl_->scale_factor);
            const float radius = border_node.corner_radius() * impl_->scale_factor;
            if (thickness > 0.0F && b.width > 0.0F && b.height > 0.0F) {
                const Rect inner = {
                    b.x + thickness,
                    b.y + thickness,
                    std::max(0.0F, b.width - (thickness * 2.0F)),
                    std::max(0.0F, b.height - (thickness * 2.0F)),
                };
                const float inner_radius = std::max(0.0F, radius - thickness);

                const int x0 = std::max(c_x0, static_cast<int>(std::floor(b.x)));
                const int y0 = std::max(c_y0, static_cast<int>(std::floor(b.y)));
                const int x1 = std::min(c_x1, static_cast<int>(std::ceil(b.right())));
                const int y1 = std::min(c_y1, static_cast<int>(std::ceil(b.bottom())));

                for (int y = y0; y < y1 && c.a > 0.0F; ++y) {
                    int skip_x0 = x0;
                    int skip_x1 = x1;
                    intersect_opaque_span(inner, inner_radius, y, skip_x0, skip_x1);
                    for (int x = x0; x < x1; ++x) {
                        // Outer coverage minus a fully covered inner pixel is
                        // zero, regardless of clipping or inherited opacity.
                        if (x == skip_x0 && skip_x1 > skip_x0) {
                            x = skip_x1 - 1;
                            continue;
                        }
                        const float sample_x = static_cast<float>(x) + 0.5F;
                        const float sample_y = static_cast<float>(y) + 0.5F;
                        const float outer = rounded_rect_coverage(sample_x, sample_y, b, radius);
                        if (outer <= 0.0F) {
                            continue;
                        }
                        const float inner_cover =
                            (inner.width > 0.0F && inner.height > 0.0F)
                                ? rounded_rect_coverage(sample_x, sample_y, inner, inner_radius)
                                : 0.0F;
                        const float coverage =
                            clamp01(outer - inner_cover) * clip_coverage(x, y, clips);
                        if (coverage > 0.0F) {
                            blend_pixel(impl_->pixels, impl_->width, x, y, c, coverage);
                        }
                    }
                }
            }
        }

        if (node.kind() == RenderNodeKind::Text) {
            ++impl_->last_hotspot_counters.text_node_count;
            const auto& text_node = static_cast<const TextNode&>(node);
            if (impl_->text_shaper && !text_node.text().empty()) {
                auto font = renderer_font_for_shape(text_node.font(), impl_->scale_factor);
                const auto color = text_node.text_color();
                const float max_width =
                    text_node.max_width() > 0.0F
                        ? renderer_max_width_for_shape(text_node.max_width(), impl_->scale_factor)
                        : 0.0F;
                TextKey key{text_node.text(),
                            font.family,
                            font.size,
                            static_cast<int>(font.weight),
                            static_cast<int>(font.style),
                            color.r,
                            color.g,
                            color.b,
                            color.a,
                            max_width,
                            impl_->scale_factor};

                std::shared_ptr<ShapedText> shaped_ptr;
                auto it = impl_->shaped_text_cache.find(key);
                if (it != impl_->shaped_text_cache.end()) {
                    shaped_ptr = it->second;
                    ++impl_->last_hotspot_counters.text_shape_cache_hit_count;
                } else {
                    if (impl_->shaped_text_cache.size() >= 1024) {
                        impl_->shaped_text_cache.clear();
                    }
                    auto s = max_width > 0.0F
                                 ? impl_->text_shaper->shape_wrapped(
                                       text_node.text(), font, color, max_width)
                                 : impl_->text_shaper->shape(text_node.text(), font, color);
                    shaped_ptr = std::make_shared<ShapedText>(std::move(s));
                    impl_->shaped_text_cache[key] = shaped_ptr;
                    ++impl_->last_hotspot_counters.text_shape_count;
                }

                const auto* bmp = shaped_ptr->bitmap_data();
                int bw = shaped_ptr->bitmap_width();
                int bh = shaped_ptr->bitmap_height();
                if (bmp && bw > 0 && bh > 0) {
                    impl_->last_hotspot_counters.text_bitmap_pixel_count +=
                        static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh);
                    const auto origin = device_point(text_node.bounds().origin());
                    int dx0 = std::max(c_x0, static_cast<int>(std::floor(origin.x)));
                    int dy0 = std::max(c_y0, static_cast<int>(std::floor(origin.y)));
                    int start_sy = dy0 - static_cast<int>(std::floor(origin.y));
                    int start_sx = dx0 - static_cast<int>(std::floor(origin.x));

                    int dy1 = std::min(c_y1, static_cast<int>(std::floor(origin.y)) + bh);
                    int dx1 = std::min(c_x1, static_cast<int>(std::floor(origin.x)) + bw);

                    for (int dy = dy0; dy < dy1; ++dy) {
                        int sy = start_sy + (dy - dy0);
                        for (int dx = dx0; dx < dx1; ++dx) {
                            int sx = start_sx + (dx - dx0);
                            auto src_idx =
                                (static_cast<std::size_t>(sy) * static_cast<std::size_t>(bw) +
                                 static_cast<std::size_t>(sx)) *
                                4U;
                            uint8_t sr = bmp[src_idx + 0];
                            uint8_t sg = bmp[src_idx + 1];
                            uint8_t sb = bmp[src_idx + 2];
                            uint8_t sa = bmp[src_idx + 3];
                            if (sa == 0) {
                                continue;
                            }
                            const float coverage = clip_coverage(dx, dy, clips);
                            if (coverage <= 0.0F) {
                                continue;
                            }
                            blend_pixel(impl_->pixels,
                                        impl_->width,
                                        dx,
                                        dy,
                                        Color{
                                            sr / 255.0F,
                                            sg / 255.0F,
                                            sb / 255.0F,
                                            (sa / 255.0F) * opacity,
                                        },
                                        coverage);
                        }
                    }
                }
            }
        }

        if (node.kind() == RenderNodeKind::Image) {
            const auto& image_node = static_cast<const ImageNode&>(node);
            const auto b = device_rect(image_node.bounds());
            const auto* src = image_node.pixel_data();
            const int src_w = image_node.src_width();
            const int src_h = image_node.src_height();

            if (src && src_w > 0 && src_h > 0) {
                ++impl_->last_hotspot_counters.image_node_count;
                impl_->last_hotspot_counters.image_source_pixel_count +=
                    static_cast<std::size_t>(src_w) * static_cast<std::size_t>(src_h);
                impl_->last_hotspot_counters.image_dest_pixel_count +=
                    scaled_pixel_area(image_node.bounds(), impl_->scale_factor);
                const int dest_x = std::max(c_x0, static_cast<int>(std::floor(b.x)));
                const int dest_y = std::max(c_y0, static_cast<int>(std::floor(b.y)));
                const int dest_r = std::min(c_x1, static_cast<int>(std::ceil(b.right())));
                const int dest_b = std::min(c_y1, static_cast<int>(std::ceil(b.bottom())));
                const int dest_w = dest_r - dest_x;
                const int dest_h = dest_b - dest_y;

                if (dest_w > 0 && dest_h > 0 && b.width > 0.0F && b.height > 0.0F) {
                    const float u_step = 1.0F / b.width;
                    const float v_step = 1.0F / b.height;
                    for (int dy = dest_y; dy < dest_b; ++dy) {
                        const float v = clamp01(((static_cast<float>(dy) + 0.5F) - b.y) * v_step);
                        const int sy = std::min(src_h - 1, static_cast<int>(v * src_h));
                        const auto row_offset =
                            static_cast<std::size_t>(sy) * static_cast<std::size_t>(src_w);

                        for (int dx = dest_x; dx < dest_r; ++dx) {
                            const float u =
                                clamp01(((static_cast<float>(dx) + 0.5F) - b.x) * u_step);
                            const int sx = std::min(src_w - 1, static_cast<int>(u * src_w));
                            const uint32_t pixel = src[row_offset + sx]; // ARGB8888
                            const float coverage = clip_coverage(dx, dy, clips);
                            if (coverage <= 0.0F) {
                                continue;
                            }
                            blend_pixel(impl_->pixels,
                                        impl_->width,
                                        dx,
                                        dy,
                                        Color{
                                            ((pixel >> 16) & 0xFF) / 255.0F,
                                            ((pixel >> 8) & 0xFF) / 255.0F,
                                            (pixel & 0xFF) / 255.0F,
                                            (((pixel >> 24) & 0xFF) / 255.0F) * opacity,
                                        },
                                        coverage);
                        }
                    }
                }
            }
        }

        for (const auto& child : node.children()) {
            self(self, *child, clips, c_x0, c_y0, c_x1, c_y1, opacity);
        }
    };

    std::vector<ClipRegion> clips;
    if (impl_->full_redraw || impl_->damage_regions.empty()) {
        render_node(
            render_node, root, clips, 0, 0, impl_->width, impl_->height, impl_->initial_opacity);
    } else {
        for (const auto& damage : impl_->damage_regions) {
            render_node(render_node,
                        root,
                        clips,
                        static_cast<int>(std::floor(damage.x)),
                        static_cast<int>(std::floor(damage.y)),
                        static_cast<int>(std::ceil(damage.right())),
                        static_cast<int>(std::ceil(damage.bottom())),
                        impl_->initial_opacity);
        }
    }
}

void SoftwareRenderer::end_frame() {
    // No-op for CPU renderer. A real backend would blit to the surface.
}

void SoftwareRenderer::present(NativeSurface& surface) {
    impl_->last_hotspot_counters.gpu_present_path = GpuPresentPath::SoftwareUpload;
    const auto damage = present_damage_regions();
    impl_->last_hotspot_counters.gpu_present_region_count = damage.has_value() ? damage->size() : 0;
    impl_->last_hotspot_counters.gpu_present_tradeoff =
        damage.has_value() ? GpuPresentTradeoff::BandwidthFavored : GpuPresentTradeoff::None;
    surface.present(
        pixel_data(), pixel_width(), pixel_height(), damage.value_or(std::span<const Rect>{}));
}

RenderHotspotCounters SoftwareRenderer::last_hotspot_counters() const {
    return impl_->last_hotspot_counters;
}

std::optional<FramePixels> SoftwareRenderer::read_back_frame() {
    if (impl_->width <= 0 || impl_->height <= 0 || impl_->pixels.empty()) {
        return std::nullopt;
    }
    return FramePixels{
        .width = impl_->width,
        .height = impl_->height,
        .rgba = impl_->pixels,
    };
}

void SoftwareRenderer::set_text_shaper(TextShaper* shaper) {
    impl_->text_shaper = shaper;
}

const uint8_t* SoftwareRenderer::pixel_data() const {
    return impl_->pixels.data();
}

int SoftwareRenderer::pixel_width() const {
    return impl_->width;
}

int SoftwareRenderer::pixel_height() const {
    return impl_->height;
}

std::optional<std::span<const Rect>> SoftwareRenderer::present_damage_regions() const {
    if (impl_->full_redraw || impl_->damage_regions.empty()) {
        return std::nullopt;
    }
    return std::span<const Rect>(impl_->damage_regions);
}

std::unique_ptr<Renderer> create_renderer(RendererBackend backend) {
    switch (backend) {
    case RendererBackend::Software:
        return std::make_unique<SoftwareRenderer>();
    case RendererBackend::D3D11:
#if defined(_WIN32)
        return create_d3d11_renderer();
#else
        return nullptr;
#endif
    case RendererBackend::D3D12:
#if defined(_WIN32)
        return create_d3d12_renderer();
#else
        return nullptr;
#endif
    case RendererBackend::Metal:
#if defined(__APPLE__)
        return create_metal_renderer();
#else
        return nullptr;
#endif
    case RendererBackend::OpenGL:
        return nullptr;
    case RendererBackend::Vulkan:
#if defined(NK_HAVE_VULKAN) && (defined(__linux__) || defined(_WIN32))
        return create_vulkan_renderer();
#else
        return nullptr;
#endif
    }
    return nullptr;
}

} // namespace nk
