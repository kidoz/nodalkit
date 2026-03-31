#include <algorithm>
#include <cmath>
#include <cstring>
#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <nk/render/renderer.h>
#include <nk/text/text_shaper.h>
#include <vector>

namespace nk {

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

float clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
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
    const float inv_a = 1.0F - alpha;
    pixels[idx + 0] = static_cast<uint8_t>(std::clamp(
        (color.r * 255.0F * alpha) + (static_cast<float>(pixels[idx + 0]) * inv_a), 0.0F, 255.0F));
    pixels[idx + 1] = static_cast<uint8_t>(std::clamp(
        (color.g * 255.0F * alpha) + (static_cast<float>(pixels[idx + 1]) * inv_a), 0.0F, 255.0F));
    pixels[idx + 2] = static_cast<uint8_t>(std::clamp(
        (color.b * 255.0F * alpha) + (static_cast<float>(pixels[idx + 2]) * inv_a), 0.0F, 255.0F));
    pixels[idx + 3] = 255;
}

} // namespace

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

// --- SoftwareRenderer ---

struct SoftwareRenderer::Impl {
    Size logical_viewport;
    float scale_factor = 1.0F;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA8
    TextShaper* text_shaper = nullptr;
};

SoftwareRenderer::SoftwareRenderer() : impl_(std::make_unique<Impl>()) {}

SoftwareRenderer::~SoftwareRenderer() = default;

void SoftwareRenderer::begin_frame(Size viewport, float scale_factor) {
    impl_->logical_viewport = viewport;
    impl_->scale_factor = normalize_scale_factor(scale_factor);
    impl_->width = scaled_extent(viewport.width, impl_->scale_factor);
    impl_->height = scaled_extent(viewport.height, impl_->scale_factor);
    const auto size = static_cast<std::size_t>(impl_->width * impl_->height * 4);
    impl_->pixels.resize(size);
    // Clear to white.
    std::memset(impl_->pixels.data(), 0xFF, impl_->pixels.size());
}

void SoftwareRenderer::render(const RenderNode& root) {
    auto render_node =
        [&](auto&& self, const RenderNode& node, std::vector<ClipRegion>& clips) -> void {
        if (node.kind() == RenderNodeKind::RoundedClip) {
            const auto& clip_node = static_cast<const RoundedClipNode&>(node);
            clips.push_back({.bounds = scale_rect(clip_node.bounds(), impl_->scale_factor),
                             .corner_radius = clip_node.corner_radius() * impl_->scale_factor});
            for (const auto& child : node.children()) {
                self(self, *child, clips);
            }
            clips.pop_back();
            return;
        }

        if (node.kind() == RenderNodeKind::ColorRect) {
            const auto& color_node = static_cast<const ColorRectNode&>(node);
            const auto b = scale_rect(color_node.bounds(), impl_->scale_factor);
            const auto c = color_node.color();

            const int x0 = std::max(0, static_cast<int>(std::floor(b.x)));
            const int y0 = std::max(0, static_cast<int>(std::floor(b.y)));
            const int x1 = std::min(impl_->width, static_cast<int>(std::ceil(b.right())));
            const int y1 = std::min(impl_->height, static_cast<int>(std::ceil(b.bottom())));

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

            const int x0 = std::max(0, static_cast<int>(std::floor(b.x)));
            const int y0 = std::max(0, static_cast<int>(std::floor(b.y)));
            const int x1 = std::min(impl_->width, static_cast<int>(std::ceil(b.right())));
            const int y1 = std::min(impl_->height, static_cast<int>(std::ceil(b.bottom())));

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

                const int x0 = std::max(0, static_cast<int>(std::floor(b.x)));
                const int y0 = std::max(0, static_cast<int>(std::floor(b.y)));
                const int x1 = std::min(impl_->width, static_cast<int>(std::ceil(b.right())));
                const int y1 = std::min(impl_->height, static_cast<int>(std::ceil(b.bottom())));

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
            const auto& text_node = static_cast<const TextNode&>(node);
            if (impl_->text_shaper && !text_node.text().empty()) {
                auto font = text_node.font();
                font.size *= impl_->scale_factor;
                auto shaped =
                    impl_->text_shaper->shape(text_node.text(), font, text_node.text_color());
                const auto* bmp = shaped.bitmap_data();
                int bw = shaped.bitmap_width();
                int bh = shaped.bitmap_height();
                if (bmp && bw > 0 && bh > 0) {
                    const auto origin =
                        scale_point(text_node.bounds().origin(), impl_->scale_factor);
                    int dx0 = std::max(0, static_cast<int>(std::floor(origin.x)));
                    int dy0 = std::max(0, static_cast<int>(std::floor(origin.y)));
                    for (int sy = 0; sy < bh && (dy0 + sy) < impl_->height; ++sy) {
                        for (int sx = 0; sx < bw && (dx0 + sx) < impl_->width; ++sx) {
                            auto src_idx = static_cast<std::size_t>((sy * bw + sx) * 4);
                            uint8_t sr = bmp[src_idx + 0];
                            uint8_t sg = bmp[src_idx + 1];
                            uint8_t sb = bmp[src_idx + 2];
                            uint8_t sa = bmp[src_idx + 3];
                            if (sa == 0) {
                                continue;
                            }
                            const float coverage = clip_coverage(dx0 + sx, dy0 + sy, clips);
                            if (coverage <= 0.0F) {
                                continue;
                            }
                            blend_pixel(impl_->pixels,
                                        impl_->width,
                                        dx0 + sx,
                                        dy0 + sy,
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
                const int dest_x = std::max(0, static_cast<int>(std::floor(b.x)));
                const int dest_y = std::max(0, static_cast<int>(std::floor(b.y)));
                const int dest_r = std::min(impl_->width, static_cast<int>(std::ceil(b.right())));
                const int dest_b = std::min(impl_->height, static_cast<int>(std::ceil(b.bottom())));
                const int dest_w = dest_r - dest_x;
                const int dest_h = dest_b - dest_y;

                if (dest_w > 0 && dest_h > 0) {
                    for (int dy = dest_y; dy < dest_b; ++dy) {
                        for (int dx = dest_x; dx < dest_r; ++dx) {
                            const float u =
                                clamp01(((static_cast<float>(dx) + 0.5F) - b.x) / b.width);
                            const float v =
                                clamp01(((static_cast<float>(dy) + 0.5F) - b.y) / b.height);
                            const int sx = std::min(src_w - 1, static_cast<int>(u * src_w));
                            const int sy = std::min(src_h - 1, static_cast<int>(v * src_h));
                            const uint32_t pixel = src[sy * src_w + sx]; // ARGB8888
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
            self(self, *child, clips);
        }
    };

    std::vector<ClipRegion> clips;
    render_node(render_node, root, clips);
}

void SoftwareRenderer::end_frame() {
    // No-op for CPU renderer. A real backend would blit to the surface.
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

} // namespace nk
