#include <cmath>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/image_view.h>
#include <vector>

namespace nk {

namespace {

Rect fit_rect(Rect bounds, int src_width, int src_height, bool preserve_aspect_ratio) {
    if (!preserve_aspect_ratio || src_width <= 0 || src_height <= 0 || bounds.width <= 0.0F ||
        bounds.height <= 0.0F) {
        return bounds;
    }

    const float scale = std::min(bounds.width / static_cast<float>(src_width),
                                 bounds.height / static_cast<float>(src_height));
    const float fitted_width = static_cast<float>(src_width) * scale;
    const float fitted_height = static_cast<float>(src_height) * scale;
    return {
        bounds.x + ((bounds.width - fitted_width) * 0.5F),
        bounds.y + ((bounds.height - fitted_height) * 0.5F),
        fitted_width,
        fitted_height,
    };
}

Rect integer_fit_rect(Rect bounds, int src_width, int src_height) {
    if (src_width <= 0 || src_height <= 0 || bounds.width <= 0.0F || bounds.height <= 0.0F) {
        return bounds;
    }
    const int scale_x = static_cast<int>(bounds.width) / src_width;
    const int scale_y = static_cast<int>(bounds.height) / src_height;
    const int scale = std::max(1, std::min(scale_x, scale_y));
    const float fitted_width = static_cast<float>(src_width * scale);
    const float fitted_height = static_cast<float>(src_height * scale);
    return {
        std::floor(bounds.x + (bounds.width - fitted_width) * 0.5F),
        std::floor(bounds.y + (bounds.height - fitted_height) * 0.5F),
        fitted_width,
        fitted_height,
    };
}

bool rect_is_empty(Rect rect) {
    return rect.width <= 0.0F || rect.height <= 0.0F;
}

Rect union_rect(Rect lhs, Rect rhs) {
    if (rect_is_empty(lhs)) {
        return rhs;
    }
    if (rect_is_empty(rhs)) {
        return lhs;
    }

    const float x0 = std::min(lhs.x, rhs.x);
    const float y0 = std::min(lhs.y, rhs.y);
    const float x1 = std::max(lhs.right(), rhs.right());
    const float y1 = std::max(lhs.bottom(), rhs.bottom());
    return {x0, y0, std::max(0.0F, x1 - x0), std::max(0.0F, y1 - y0)};
}

Rect local_image_content_rect(Rect allocation,
                              int src_width,
                              int src_height,
                              bool preserve_aspect_ratio) {
    const Rect inner = {
        1.0F,
        1.0F,
        std::max(0.0F, allocation.width - 2.0F),
        std::max(0.0F, allocation.height - 2.0F),
    };
    if (rect_is_empty(inner) || src_width <= 0 || src_height <= 0) {
        return {};
    }
    return fit_rect(inner, src_width, src_height, preserve_aspect_ratio);
}

} // namespace

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

ImageView::ImageView() : impl_(std::make_unique<Impl>()) {
    add_style_class("image-view");
    ensure_accessible().set_role(AccessibleRole::Image);
}

ImageView::~ImageView() = default;

void ImageView::update_pixel_buffer(const uint32_t* data, int width, int height) {
    const auto a = allocation();
    Rect damage{};
    {
        std::lock_guard lock(impl_->mutex);
        const auto previous_damage = local_image_content_rect(
            a, impl_->src_width, impl_->src_height, impl_->preserve_aspect_ratio);
        const auto count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
        impl_->pixels.assign(data, data + count);
        impl_->src_width = width;
        impl_->src_height = height;
        damage = union_rect(previous_damage,
                            local_image_content_rect(a,
                                                     impl_->src_width,
                                                     impl_->src_height,
                                                     impl_->preserve_aspect_ratio));
    }
    // queue_redraw can fan out to the Window / damage tracker on the main thread; keeping the
    // mutex across that call risks lock-order inversions with anything those paths touch. The
    // damage rect was captured under the lock so the release is safe here.
    if (rect_is_empty(damage)) {
        queue_redraw();
    } else {
        queue_redraw(damage);
    }
}

void ImageView::set_scale_mode(ScaleMode mode) {
    if (impl_->scale_mode != mode) {
        impl_->scale_mode = mode;
        const auto damage = local_image_content_rect(
            allocation(), source_width(), source_height(), impl_->preserve_aspect_ratio);
        if (rect_is_empty(damage)) {
            queue_redraw();
        } else {
            queue_redraw(damage);
        }
    }
}

ScaleMode ImageView::scale_mode() const {
    return impl_->scale_mode;
}

void ImageView::set_preserve_aspect_ratio(bool preserve) {
    if (impl_->preserve_aspect_ratio != preserve) {
        const auto previous_damage =
            local_image_content_rect(allocation(), source_width(), source_height(), impl_->preserve_aspect_ratio);
        impl_->preserve_aspect_ratio = preserve;
        queue_layout();
        const auto damage = union_rect(
            previous_damage,
            local_image_content_rect(allocation(), source_width(), source_height(), impl_->preserve_aspect_ratio));
        if (rect_is_empty(damage)) {
            queue_redraw();
        } else {
            queue_redraw(damage);
        }
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

SizeRequest ImageView::measure(const Constraints& constraints) const {
    std::lock_guard lock(impl_->mutex);
    const float min_height = theme_number("min-height", 168.0F);
    float w = std::max(180.0F, static_cast<float>(impl_->src_width));
    float h = std::max(min_height, static_cast<float>(impl_->src_height));
    // Clamp the natural size to the available space so parents with fixed widths (toolbars,
    // status panels) don't end up asking the layout system for a pixel-perfect image size that
    // overflows. preserve_aspect_ratio is still honored at draw time via fit_rect.
    if (constraints.max_width > 0.0F) {
        w = std::min(w, constraints.max_width);
    }
    if (constraints.max_height > 0.0F) {
        h = std::min(h, constraints.max_height);
    }
    return {0, 0, w, h};
}

void ImageView::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 14.0F);
    const float content_radius = theme_number("content-radius", 12.0F);

    ctx.add_rounded_rect({a.x, a.y + 1.0F, a.width, a.height},
                         Color{0.08F, 0.12F, 0.18F, 0.04F},
                         corner_radius + 1.0F);
    ctx.add_rounded_rect(
        a, theme_color("background", Color{0.95F, 0.96F, 0.98F, 1.0F}), corner_radius);
    ctx.add_border(
        a, theme_color("border-color", Color{0.86F, 0.88F, 0.91F, 1.0F}), 1.0F, corner_radius);

    Rect inner = {
        a.x + 1.0F,
        a.y + 1.0F,
        std::max(0.0F, a.width - 2.0F),
        std::max(0.0F, a.height - 2.0F),
    };

    std::lock_guard lock(impl_->mutex);
    if (!impl_->pixels.empty() && impl_->src_width > 0 && impl_->src_height > 0) {
        note_image_snapshot_for_diagnostics();
        const bool use_integer = impl_->scale_mode == ScaleMode::IntegerNearest;
        const auto image_bounds = use_integer
            ? integer_fit_rect(inner, impl_->src_width, impl_->src_height)
            : fit_rect(inner, impl_->src_width, impl_->src_height, impl_->preserve_aspect_ratio);
        const auto effective_mode = use_integer ? ScaleMode::NearestNeighbor : impl_->scale_mode;
        ctx.push_rounded_clip(inner, content_radius);
        ctx.add_image(image_bounds,
                      impl_->pixels.data(),
                      impl_->src_width,
                      impl_->src_height,
                      effective_mode);
        ctx.pop_container();
    }
}

} // namespace nk
