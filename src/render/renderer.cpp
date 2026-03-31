#include <nk/render/renderer.h>

#include <nk/render/image_node.h>
#include <nk/render/render_node.h>
#include <nk/text/text_shaper.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace nk {

namespace {

struct ClipRegion {
    Rect bounds;
    float corner_radius = 0.0F;
};

float clamp01(float value) {
    return std::clamp(value, 0.0F, 1.0F);
}

float effective_corner_radius(Rect rect, float requested_radius) {
    return std::clamp(
        requested_radius,
        0.0F,
        std::min(rect.width, rect.height) * 0.5F);
}

float signed_distance_to_rounded_rect(
    float px,
    float py,
    Rect rect,
    float corner_radius) {
    float const radius = effective_corner_radius(rect, corner_radius);
    float const center_x = rect.x + (rect.width * 0.5F);
    float const center_y = rect.y + (rect.height * 0.5F);
    float const local_x = std::abs(px - center_x);
    float const local_y = std::abs(py - center_y);
    float const half_w = rect.width * 0.5F;
    float const half_h = rect.height * 0.5F;
    float const inner_w = std::max(0.0F, half_w - radius);
    float const inner_h = std::max(0.0F, half_h - radius);
    float const qx = local_x - inner_w;
    float const qy = local_y - inner_h;
    float const outside_x = std::max(qx, 0.0F);
    float const outside_y = std::max(qy, 0.0F);
    float const outside =
        std::sqrt((outside_x * outside_x) + (outside_y * outside_y));
    float const inside = std::min(std::max(qx, qy), 0.0F);
    return outside + inside - radius;
}

float rounded_rect_coverage(float px, float py, Rect rect, float corner_radius) {
    return clamp01(0.5F - signed_distance_to_rounded_rect(
        px, py, rect, corner_radius));
}

float clip_coverage(
    int x,
    int y,
    std::vector<ClipRegion> const& clips) {
    float coverage = 1.0F;
    float const sample_x = static_cast<float>(x) + 0.5F;
    float const sample_y = static_cast<float>(y) + 0.5F;
    for (auto const& clip : clips) {
        coverage = std::min(
            coverage,
            rounded_rect_coverage(
                sample_x, sample_y, clip.bounds, clip.corner_radius));
        if (coverage <= 0.0F) {
            return 0.0F;
        }
    }
    return coverage;
}

void blend_pixel(
    std::vector<uint8_t>& pixels,
    int pixel_width,
    int x,
    int y,
    Color color,
    float coverage = 1.0F) {
    float const alpha = clamp01(color.a * coverage);
    if (alpha <= 0.0F) {
        return;
    }

    auto const idx = static_cast<std::size_t>((y * pixel_width + x) * 4);
    float const inv_a = 1.0F - alpha;
    pixels[idx + 0] = static_cast<uint8_t>(std::clamp(
        (color.r * 255.0F * alpha)
            + (static_cast<float>(pixels[idx + 0]) * inv_a),
        0.0F,
        255.0F));
    pixels[idx + 1] = static_cast<uint8_t>(std::clamp(
        (color.g * 255.0F * alpha)
            + (static_cast<float>(pixels[idx + 1]) * inv_a),
        0.0F,
        255.0F));
    pixels[idx + 2] = static_cast<uint8_t>(std::clamp(
        (color.b * 255.0F * alpha)
            + (static_cast<float>(pixels[idx + 2]) * inv_a),
        0.0F,
        255.0F));
    pixels[idx + 3] = 255;
}

} // namespace

Renderer::Renderer() = default;
Renderer::~Renderer() = default;

// --- SoftwareRenderer ---

struct SoftwareRenderer::Impl {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels; // RGBA8
    TextShaper* text_shaper = nullptr;
};

SoftwareRenderer::SoftwareRenderer()
    : impl_(std::make_unique<Impl>()) {}

SoftwareRenderer::~SoftwareRenderer() = default;

void SoftwareRenderer::begin_frame(Size viewport) {
    impl_->width = static_cast<int>(viewport.width);
    impl_->height = static_cast<int>(viewport.height);
    auto const size =
        static_cast<std::size_t>(impl_->width * impl_->height * 4);
    impl_->pixels.resize(size);
    // Clear to white.
    std::memset(impl_->pixels.data(), 0xFF, impl_->pixels.size());
}

void SoftwareRenderer::render(RenderNode const& root) {
    auto render_node = [&](auto&& self,
                           RenderNode const& node,
                           std::vector<ClipRegion>& clips) -> void {
    if (node.kind() == RenderNodeKind::RoundedClip) {
        auto const& clip_node = static_cast<RoundedClipNode const&>(node);
        clips.push_back(
            {.bounds = clip_node.bounds(), .corner_radius = clip_node.corner_radius()});
        for (auto const& child : node.children()) {
            self(self, *child, clips);
        }
        clips.pop_back();
        return;
    }

    if (node.kind() == RenderNodeKind::ColorRect) {
        auto const& color_node = static_cast<ColorRectNode const&>(node);
        auto const b = color_node.bounds();
        auto const c = color_node.color();

        int const x0 = std::max(0, static_cast<int>(b.x));
        int const y0 = std::max(0, static_cast<int>(b.y));
        int const x1 = std::min(impl_->width, static_cast<int>(b.right()));
        int const y1 = std::min(impl_->height, static_cast<int>(b.bottom()));

        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                float const coverage = clip_coverage(x, y, clips);
                if (coverage > 0.0F) {
                    blend_pixel(impl_->pixels, impl_->width, x, y, c, coverage);
                }
            }
        }
    }

    if (node.kind() == RenderNodeKind::RoundedRect) {
        auto const& rounded_node = static_cast<RoundedRectNode const&>(node);
        auto const b = rounded_node.bounds();
        auto const c = rounded_node.color();
        float const radius = rounded_node.corner_radius();

        int const x0 = std::max(0, static_cast<int>(std::floor(b.x)));
        int const y0 = std::max(0, static_cast<int>(std::floor(b.y)));
        int const x1 = std::min(impl_->width, static_cast<int>(std::ceil(b.right())));
        int const y1 = std::min(impl_->height, static_cast<int>(std::ceil(b.bottom())));

        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                float const coverage = rounded_rect_coverage(
                    static_cast<float>(x) + 0.5F,
                    static_cast<float>(y) + 0.5F,
                    b,
                    radius);
                float const clipped = coverage * clip_coverage(x, y, clips);
                if (clipped > 0.0F) {
                    blend_pixel(impl_->pixels, impl_->width, x, y, c, clipped);
                }
            }
        }
    }

    if (node.kind() == RenderNodeKind::Border) {
        auto const& border_node = static_cast<BorderNode const&>(node);
        auto const b = border_node.bounds();
        auto const c = border_node.color();
        float const thickness = std::max(0.0F, border_node.thickness());
        float const radius = border_node.corner_radius();
        if (thickness > 0.0F && b.width > 0.0F && b.height > 0.0F) {
            Rect const inner = {
                b.x + thickness,
                b.y + thickness,
                std::max(0.0F, b.width - (thickness * 2.0F)),
                std::max(0.0F, b.height - (thickness * 2.0F)),
            };
            float const inner_radius = std::max(0.0F, radius - thickness);

            int const x0 = std::max(0, static_cast<int>(std::floor(b.x)));
            int const y0 = std::max(0, static_cast<int>(std::floor(b.y)));
            int const x1 =
                std::min(impl_->width, static_cast<int>(std::ceil(b.right())));
            int const y1 =
                std::min(impl_->height, static_cast<int>(std::ceil(b.bottom())));

            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    float const sample_x = static_cast<float>(x) + 0.5F;
                    float const sample_y = static_cast<float>(y) + 0.5F;
                    float const outer = rounded_rect_coverage(
                        sample_x, sample_y, b, radius);
                    if (outer <= 0.0F) {
                        continue;
                    }
                    float const inner_cover =
                        (inner.width > 0.0F && inner.height > 0.0F)
                            ? rounded_rect_coverage(
                                  sample_x, sample_y, inner, inner_radius)
                            : 0.0F;
                    float const coverage =
                        clamp01(outer - inner_cover) * clip_coverage(x, y, clips);
                    if (coverage > 0.0F) {
                        blend_pixel(
                            impl_->pixels, impl_->width, x, y, c, coverage);
                    }
                }
            }
        }
    }

    if (node.kind() == RenderNodeKind::Text) {
        auto const& text_node = static_cast<TextNode const&>(node);
        if (impl_->text_shaper && !text_node.text().empty()) {
            auto shaped = impl_->text_shaper->shape(
                text_node.text(), text_node.font(),
                text_node.text_color());
            auto const* bmp = shaped.bitmap_data();
            int bw = shaped.bitmap_width();
            int bh = shaped.bitmap_height();
            if (bmp && bw > 0 && bh > 0) {
                auto const b = text_node.bounds();
                int dx0 = std::max(0, static_cast<int>(b.x));
                int dy0 = std::max(0, static_cast<int>(b.y));
                for (int sy = 0; sy < bh && (dy0 + sy) < impl_->height;
                     ++sy) {
                    for (int sx = 0; sx < bw && (dx0 + sx) < impl_->width;
                         ++sx) {
                        auto src_idx =
                            static_cast<std::size_t>((sy * bw + sx) * 4);
                        uint8_t sr = bmp[src_idx + 0];
                        uint8_t sg = bmp[src_idx + 1];
                        uint8_t sb = bmp[src_idx + 2];
                        uint8_t sa = bmp[src_idx + 3];
                        if (sa == 0) continue;
                        float const coverage =
                            clip_coverage(dx0 + sx, dy0 + sy, clips);
                        if (coverage <= 0.0F) {
                            continue;
                        }
                        blend_pixel(
                            impl_->pixels,
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
        auto const& image_node = static_cast<ImageNode const&>(node);
        auto const b = image_node.bounds();
        auto const* src = image_node.pixel_data();
        int const src_w = image_node.src_width();
        int const src_h = image_node.src_height();

        if (src && src_w > 0 && src_h > 0) {
            int const dest_x = std::max(0, static_cast<int>(b.x));
            int const dest_y = std::max(0, static_cast<int>(b.y));
            int const dest_r = std::min(impl_->width,
                                        static_cast<int>(b.right()));
            int const dest_b = std::min(impl_->height,
                                        static_cast<int>(b.bottom()));
            int const dest_w = static_cast<int>(b.width);
            int const dest_h = static_cast<int>(b.height);

            if (dest_w > 0 && dest_h > 0) {
                for (int dy = dest_y; dy < dest_b; ++dy) {
                    for (int dx = dest_x; dx < dest_r; ++dx) {
                        int const sx = (dx - static_cast<int>(b.x))
                                       * src_w / dest_w;
                        int const sy = (dy - static_cast<int>(b.y))
                                       * src_h / dest_h;
                        uint32_t const pixel =
                            src[sy * src_w + sx]; // ARGB8888
                        float const coverage = clip_coverage(dx, dy, clips);
                        if (coverage <= 0.0F) {
                            continue;
                        }
                        blend_pixel(
                            impl_->pixels,
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

    for (auto const& child : node.children()) {
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

uint8_t const* SoftwareRenderer::pixel_data() const {
    return impl_->pixels.data();
}

int SoftwareRenderer::pixel_width() const { return impl_->width; }
int SoftwareRenderer::pixel_height() const { return impl_->height; }

} // namespace nk
