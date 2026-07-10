#include <algorithm>
#include <nk/widgets/clamp.h>

namespace nk {

namespace {

float smooth_step(float value) {
    value = std::clamp(value, 0.0F, 1.0F);
    return value * value * (3.0F - (2.0F * value));
}

} // namespace

struct Clamp::Impl {
    Orientation orientation = Orientation::Horizontal;
    std::shared_ptr<Widget> child;
    float maximum_size = 720.0F;
    float tightening_threshold = 540.0F;
    bool scales_with_text = true;
};

std::shared_ptr<Clamp> Clamp::create(Orientation orientation) {
    return std::shared_ptr<Clamp>(new Clamp(orientation));
}

Clamp::Clamp(Orientation orientation) : impl_(std::make_unique<Impl>()) {
    impl_->orientation = orientation;
    add_style_class("clamp");
}

Clamp::~Clamp() = default;

void Clamp::set_child(std::shared_ptr<Widget> child) {
    if (impl_->child == child) {
        return;
    }
    if (impl_->child != nullptr) {
        remove_child(*impl_->child);
    }
    impl_->child = std::move(child);
    if (impl_->child != nullptr) {
        append_child(impl_->child);
    }
    queue_layout();
}

Widget* Clamp::child() const {
    return impl_->child.get();
}

Orientation Clamp::orientation() const {
    return impl_->orientation;
}

float Clamp::maximum_size() const {
    return impl_->maximum_size;
}

void Clamp::set_maximum_size(float size) {
    size = std::max(0.0F, size);
    if (impl_->maximum_size == size) {
        return;
    }
    impl_->maximum_size = size;
    queue_layout();
}

float Clamp::tightening_threshold() const {
    return impl_->tightening_threshold;
}

void Clamp::set_tightening_threshold(float size) {
    size = std::max(0.0F, size);
    if (impl_->tightening_threshold == size) {
        return;
    }
    impl_->tightening_threshold = size;
    queue_layout();
}

bool Clamp::scales_with_text() const {
    return impl_->scales_with_text;
}

void Clamp::set_scales_with_text(bool enabled) {
    if (impl_->scales_with_text == enabled) {
        return;
    }
    impl_->scales_with_text = enabled;
    queue_layout();
}

bool Clamp::has_height_for_width() const {
    return impl_->child != nullptr && impl_->child->has_height_for_width();
}

float Clamp::height_for_width(float width) const {
    if (impl_->child == nullptr) {
        return 0.0F;
    }
    const float scale = impl_->scales_with_text ? theme_number("text-scale", 1.0F) : 1.0F;
    const float maximum = impl_->maximum_size * scale;
    const float child_width = std::min(width, maximum);
    return impl_->child->has_height_for_width()
               ? impl_->child->height_for_width(child_width)
               : impl_->child->measure(Constraints::unbounded()).natural_height;
}

SizeRequest Clamp::measure(const Constraints& constraints) const {
    if (impl_->child == nullptr) {
        return {};
    }
    auto request = impl_->child->measure_for_diagnostics(constraints);
    const float scale = impl_->scales_with_text ? theme_number("text-scale", 1.0F) : 1.0F;
    const float maximum = impl_->maximum_size * scale;
    if (impl_->orientation == Orientation::Horizontal) {
        request.natural_width = std::min(request.natural_width, maximum);
    } else {
        request.natural_height = std::min(request.natural_height, maximum);
    }
    return request;
}

void Clamp::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    if (impl_->child == nullptr) {
        return;
    }

    const float scale = impl_->scales_with_text ? theme_number("text-scale", 1.0F) : 1.0F;
    const float maximum = impl_->maximum_size * scale;
    const float threshold = std::min(impl_->tightening_threshold * scale, maximum);
    const float available =
        impl_->orientation == Orientation::Horizontal ? allocation.width : allocation.height;
    float child_main = available;
    if (available > threshold) {
        if (maximum <= threshold) {
            child_main = std::min(available, maximum);
        } else {
            const float progress = (available - threshold) / (maximum - threshold);
            child_main = threshold + ((maximum - threshold) * smooth_step(progress));
            child_main = std::min({child_main, available, maximum});
        }
    }

    if (impl_->orientation == Orientation::Horizontal) {
        impl_->child->allocate({allocation.x + ((allocation.width - child_main) * 0.5F),
                                allocation.y,
                                child_main,
                                allocation.height});
    } else {
        impl_->child->allocate({allocation.x,
                                allocation.y + ((allocation.height - child_main) * 0.5F),
                                allocation.width,
                                child_main});
    }
}

} // namespace nk
