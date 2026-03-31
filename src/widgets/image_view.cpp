#include <nk/widgets/image_view.h>

#include <nk/render/snapshot_context.h>

#include <vector>

namespace nk {

namespace {

Rect fit_rect(Rect bounds, int src_width, int src_height, bool preserve_aspect_ratio) {
    if (!preserve_aspect_ratio || src_width <= 0 || src_height <= 0
        || bounds.width <= 0.0F || bounds.height <= 0.0F) {
        return bounds;
    }

    float const scale = std::min(
        bounds.width / static_cast<float>(src_width),
        bounds.height / static_cast<float>(src_height));
    float const fitted_width = static_cast<float>(src_width) * scale;
    float const fitted_height = static_cast<float>(src_height) * scale;
    return {
        bounds.x + ((bounds.width - fitted_width) * 0.5F),
        bounds.y + ((bounds.height - fitted_height) * 0.5F),
        fitted_width,
        fitted_height,
    };
}

}

struct ImageView::Impl {
    std::vector<uint32_t> pixels;
    int src_width = 0;
    int src_height = 0;
    ScaleMode scale_mode = ScaleMode::NearestNeighbor;
    bool preserve_aspect_ratio = true;
    mutable std::mutex mutex;
};

std::shared_ptr<ImageView> ImageView::create() {
    return std::shared_ptr<ImageView>(new ImageView());
}

ImageView::ImageView()
    : impl_(std::make_unique<Impl>()) {
    add_style_class("image-view");
}

ImageView::~ImageView() = default;

void ImageView::update_pixel_buffer(uint32_t const* data,
                                    int width, int height) {
    std::lock_guard lock(impl_->mutex);
    auto const count = static_cast<std::size_t>(width) *
                       static_cast<std::size_t>(height);
    impl_->pixels.assign(data, data + count);
    impl_->src_width = width;
    impl_->src_height = height;
    queue_redraw();
}

void ImageView::set_scale_mode(ScaleMode mode) {
    if (impl_->scale_mode != mode) {
        impl_->scale_mode = mode;
        queue_redraw();
    }
}

ScaleMode ImageView::scale_mode() const {
    return impl_->scale_mode;
}

void ImageView::set_preserve_aspect_ratio(bool preserve) {
    if (impl_->preserve_aspect_ratio != preserve) {
        impl_->preserve_aspect_ratio = preserve;
        queue_layout();
        queue_redraw();
    }
}

bool ImageView::preserve_aspect_ratio() const {
    return impl_->preserve_aspect_ratio;
}

int ImageView::source_width() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->src_width;
}

int ImageView::source_height() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->src_height;
}

SizeRequest ImageView::measure(Constraints const& /*constraints*/) const {
    std::lock_guard lock(impl_->mutex);
    float const min_height = theme_number("min-height", 168.0F);
    float const w = std::max(180.0F, static_cast<float>(impl_->src_width));
    float const h = std::max(min_height, static_cast<float>(impl_->src_height));
    return {0, 0, w, h};
}

void ImageView::snapshot(SnapshotContext& ctx) const {
    auto const a = allocation();
    float const corner_radius = theme_number("corner-radius", 14.0F);
    float const content_radius = theme_number("content-radius", 12.0F);

    ctx.add_rounded_rect(
        {a.x, a.y + 1.0F, a.width, a.height},
        Color{0.08F, 0.12F, 0.18F, 0.04F},
        corner_radius + 1.0F);
    ctx.add_rounded_rect(
        a,
        theme_color("background", Color{0.95F, 0.96F, 0.98F, 1.0F}),
        corner_radius);
    ctx.add_border(
        a,
        theme_color("border-color", Color{0.86F, 0.88F, 0.91F, 1.0F}),
        1.0F,
        corner_radius);

    Rect inner = {
        a.x + 1.0F,
        a.y + 1.0F,
        std::max(0.0F, a.width - 2.0F),
        std::max(0.0F, a.height - 2.0F),
    };

    std::lock_guard lock(impl_->mutex);
    if (!impl_->pixels.empty() && impl_->src_width > 0
        && impl_->src_height > 0) {
        auto const image_bounds = fit_rect(
            inner,
            impl_->src_width,
            impl_->src_height,
            impl_->preserve_aspect_ratio);
        ctx.push_rounded_clip(inner, content_radius);
        ctx.add_image(
            image_bounds,
            impl_->pixels.data(),
            impl_->src_width,
            impl_->src_height,
            impl_->scale_mode);
        ctx.pop_container();
    }
}

} // namespace nk
