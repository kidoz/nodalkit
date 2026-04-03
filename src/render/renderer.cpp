#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <nk/platform/platform_backend.h>
#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <nk/render/renderer.h>
#include <nk/text/shaped_text.h>
#include <nk/text/text_shaper.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace nk {

#if defined(__APPLE__)
std::unique_ptr<Renderer> create_metal_renderer();
#endif

#if defined(NK_HAVE_VULKAN) && defined(__linux__)
std::unique_ptr<Renderer> create_vulkan_renderer();
#endif

namespace {

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
    if (logical_extent <= 0.0F) {
        return 0;
    }

    return std::max(1, static_cast<int>(std::lround(logical_extent * scale_factor)));
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

    const auto idx = static_cast<std::size_t>((y * pixel_width + x) * 4);

    // Fast-path: opaque pixel overwrite
    if (alpha >= 0.999F) {
        pixels[idx + 0] = static_cast<uint8_t>(std::clamp(color.r * 255.0F, 0.0F, 255.0F));
        pixels[idx + 1] = static_cast<uint8_t>(std::clamp(color.g * 255.0F, 0.0F, 255.0F));
        pixels[idx + 2] = static_cast<uint8_t>(std::clamp(color.b * 255.0F, 0.0F, 255.0F));
        pixels[idx + 3] = 255;
        return;
    }

    const float inv_a = 1.0F - alpha;
    pixels[idx + 0] = static_cast<uint8_t>(std::clamp(
        (color.r * 255.0F * alpha) + (static_cast<float>(pixels[idx + 0]) * inv_a), 0.0F, 255.0F));
    pixels[idx + 1] = static_cast<uint8_t>(std::clamp(
        (color.g * 255.0F * alpha) + (static_cast<float>(pixels[idx + 1]) * inv_a), 0.0F, 255.0F));
    pixels[idx + 2] = static_cast<uint8_t>(std::clamp(
        (color.b * 255.0F * alpha) + (static_cast<float>(pixels[idx + 2]) * inv_a), 0.0F, 255.0F));
    pixels[idx + 3] = 255;
}

struct TextKey {
    std::string text;
    std::string family;
    float size;
    int weight;
    int style;
    float r, g, b, a;
    float scale;

    bool operator==(const TextKey& o) const {
        return text == o.text && family == o.family && size == o.size && weight == o.weight &&
               style == o.style && r == o.r && g == o.g && b == o.b && a == o.a && scale == o.scale;
    }
};

struct TextKeyHash {
    std::size_t operator()(const TextKey& k) const {
        std::size_t h = std::hash<std::string>{}(k.text);
        h ^= std::hash<std::string>{}(k.family) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<float>{}(k.size) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(k.weight) + 0x9e3779b9 + (h << 6) + (h >> 2);
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
    case RendererBackend::Metal:
#if defined(__APPLE__)
        return true;
#else
        return false;
#endif
    case RendererBackend::OpenGL:
        return false;
    case RendererBackend::Vulkan:
#if defined(NK_HAVE_VULKAN) && defined(__linux__)
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
    impl_->logical_viewport = viewport;
    impl_->scale_factor = normalize_scale_factor(scale_factor);
    const int next_width = scaled_extent(viewport.width, impl_->scale_factor);
    const int next_height = scaled_extent(viewport.height, impl_->scale_factor);
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

    const auto size =
        static_cast<std::size_t>(std::max(0, impl_->width) * std::max(0, impl_->height) * 4);
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
    auto clear_rect = [&](Rect rect) {
        const int x0 = std::max(0, static_cast<int>(std::floor(rect.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(rect.y)));
        const int x1 = std::min(impl_->width, static_cast<int>(std::ceil(rect.right())));
        const int y1 = std::min(impl_->height, static_cast<int>(std::ceil(rect.bottom())));
        if (x1 <= x0 || y1 <= y0) {
            return;
        }

        for (int y = y0; y < y1; ++y) {
            auto* row =
                impl_->pixels.data() + static_cast<std::size_t>((y * impl_->width + x0) * 4);
            std::memset(row, 0xFF, static_cast<std::size_t>(x1 - x0) * 4);
        }
    };

    if (impl_->full_redraw || impl_->damage_regions.empty()) {
        if (!impl_->pixels.empty()) {
            std::memset(impl_->pixels.data(), 0xFF, impl_->pixels.size());
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
                           int c_y1) -> void {
        if (c_x1 <= c_x0 || c_y1 <= c_y0) {
            return;
        }

        if (node.kind() != RenderNodeKind::Text) {
            const auto scaled_bounds = scale_rect(node.bounds(), impl_->scale_factor);
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
            const auto scaled_bounds = scale_rect(clip_node.bounds(), impl_->scale_factor);
            clips.push_back({.bounds = scaled_bounds,
                             .corner_radius = clip_node.corner_radius() * impl_->scale_factor});
            int next_x0 = std::max(c_x0, static_cast<int>(std::floor(scaled_bounds.x)));
            int next_y0 = std::max(c_y0, static_cast<int>(std::floor(scaled_bounds.y)));
            int next_x1 = std::min(c_x1, static_cast<int>(std::ceil(scaled_bounds.right())));
            int next_y1 = std::min(c_y1, static_cast<int>(std::ceil(scaled_bounds.bottom())));

            for (const auto& child : node.children()) {
                self(self, *child, clips, next_x0, next_y0, next_x1, next_y1);
            }
            clips.pop_back();
            return;
        }

        if (node.kind() == RenderNodeKind::ColorRect) {
            const auto& color_node = static_cast<const ColorRectNode&>(node);
            const auto b = scale_rect(color_node.bounds(), impl_->scale_factor);
            const auto c = color_node.color();

            const int x0 = std::max(c_x0, static_cast<int>(std::floor(b.x)));
            const int y0 = std::max(c_y0, static_cast<int>(std::floor(b.y)));
            const int x1 = std::min(c_x1, static_cast<int>(std::ceil(b.right())));
            const int y1 = std::min(c_y1, static_cast<int>(std::ceil(b.bottom())));

            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const float coverage = clip_coverage(x, y, clips);
                    if (coverage > 0.0F) {
                        blend_pixel(impl_->pixels, impl_->width, x, y, c, coverage);
                    }
                }
            }
        }

        if (node.kind() == RenderNodeKind::RoundedRect) {
            const auto& rounded_node = static_cast<const RoundedRectNode&>(node);
            const auto b = scale_rect(rounded_node.bounds(), impl_->scale_factor);
            const auto c = rounded_node.color();
            const float radius = rounded_node.corner_radius() * impl_->scale_factor;

            const int x0 = std::max(c_x0, static_cast<int>(std::floor(b.x)));
            const int y0 = std::max(c_y0, static_cast<int>(std::floor(b.y)));
            const int x1 = std::min(c_x1, static_cast<int>(std::ceil(b.right())));
            const int y1 = std::min(c_y1, static_cast<int>(std::ceil(b.bottom())));

            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
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
            const auto b = scale_rect(border_node.bounds(), impl_->scale_factor);
            const auto c = border_node.color();
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

                for (int y = y0; y < y1; ++y) {
                    for (int x = x0; x < x1; ++x) {
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
                auto font = text_node.font();
                font.size *= impl_->scale_factor;
                const auto color = text_node.text_color();
                TextKey key{text_node.text(),
                            font.family,
                            font.size,
                            static_cast<int>(font.weight),
                            static_cast<int>(font.style),
                            color.r,
                            color.g,
                            color.b,
                            color.a,
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
                    auto s = impl_->text_shaper->shape(text_node.text(), font, color);
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
                    const auto origin =
                        scale_point(text_node.bounds().origin(), impl_->scale_factor);
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
                            auto src_idx = static_cast<std::size_t>((sy * bw + sx) * 4);
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
                                            sa / 255.0F,
                                        },
                                        coverage);
                        }
                    }
                }
            }
        }

        if (node.kind() == RenderNodeKind::Image) {
            const auto& image_node = static_cast<const ImageNode&>(node);
            const auto b = scale_rect(image_node.bounds(), impl_->scale_factor);
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
                        const int row_offset = sy * src_w;

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
                                            ((pixel >> 24) & 0xFF) / 255.0F,
                                        },
                                        coverage);
                        }
                    }
                }
            }
        }

        for (const auto& child : node.children()) {
            self(self, *child, clips, c_x0, c_y0, c_x1, c_y1);
        }
    };

    std::vector<ClipRegion> clips;
    if (impl_->full_redraw || impl_->damage_regions.empty()) {
        render_node(render_node, root, clips, 0, 0, impl_->width, impl_->height);
    } else {
        for (const auto& damage : impl_->damage_regions) {
            render_node(render_node,
                        root,
                        clips,
                        static_cast<int>(std::floor(damage.x)),
                        static_cast<int>(std::floor(damage.y)),
                        static_cast<int>(std::ceil(damage.right())),
                        static_cast<int>(std::ceil(damage.bottom())));
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
    case RendererBackend::Metal:
#if defined(__APPLE__)
        return create_metal_renderer();
#else
        return nullptr;
#endif
    case RendererBackend::OpenGL:
        return nullptr;
    case RendererBackend::Vulkan:
#if defined(NK_HAVE_VULKAN) && defined(__linux__)
        return create_vulkan_renderer();
#else
        return nullptr;
#endif
    }
    return nullptr;
}

} // namespace nk
