#include <algorithm>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/avatar.h>

namespace nk {

namespace {

FontDescriptor avatar_font(float diameter) {
    return FontDescriptor{
        .family = {},
        .size = diameter * 0.4F,
        .weight = FontWeight::Bold,
    };
}

} // namespace

struct Avatar::Impl {
    std::string initials;
    float diameter = 40.0F;
    const uint32_t* image_data = nullptr;
    int image_width = 0;
    int image_height = 0;
    AvatarStatus status = AvatarStatus::None;
};

namespace {

const char* status_name(AvatarStatus status) {
    switch (status) {
    case AvatarStatus::Online:
        return "Online";
    case AvatarStatus::Busy:
        return "Busy";
    case AvatarStatus::Away:
        return "Away";
    case AvatarStatus::Offline:
        return "Offline";
    case AvatarStatus::None:
        return "";
    }
    return "";
}

Color status_color(AvatarStatus status) {
    switch (status) {
    case AvatarStatus::Online:
        return Color{0.18F, 0.72F, 0.35F, 1.0F};
    case AvatarStatus::Busy:
        return Color{0.86F, 0.22F, 0.24F, 1.0F};
    case AvatarStatus::Away:
        return Color{0.92F, 0.62F, 0.16F, 1.0F};
    case AvatarStatus::Offline:
        return Color{0.55F, 0.58F, 0.62F, 1.0F};
    case AvatarStatus::None:
        return Color{};
    }
    return Color{};
}

} // namespace

std::shared_ptr<Avatar> Avatar::create(std::string initials) {
    return std::shared_ptr<Avatar>(new Avatar(std::move(initials)));
}

Avatar::Avatar(std::string initials) : impl_(std::make_unique<Impl>()) {
    impl_->initials = std::move(initials);
    add_style_class("avatar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Image);
    accessible.set_name(impl_->initials.empty() ? "Avatar" : impl_->initials);
}

Avatar::~Avatar() = default;

std::string_view Avatar::initials() const {
    return impl_->initials;
}

void Avatar::set_initials(std::string initials) {
    if (impl_->initials != initials) {
        impl_->initials = std::move(initials);
        ensure_accessible().set_name(impl_->initials.empty() ? "Avatar" : impl_->initials);
        queue_redraw();
    }
}

void Avatar::set_image(const uint32_t* data, int width, int height) {
    impl_->image_data = data;
    impl_->image_width = width;
    impl_->image_height = height;
    queue_redraw();
}

void Avatar::clear_image() {
    impl_->image_data = nullptr;
    impl_->image_width = 0;
    impl_->image_height = 0;
    queue_redraw();
}

void Avatar::set_status(AvatarStatus status) {
    if (impl_->status == status) {
        return;
    }
    impl_->status = status;
    ensure_accessible().set_description(status_name(status));
    queue_redraw();
}

AvatarStatus Avatar::status() const {
    return impl_->status;
}

float Avatar::diameter() const {
    return impl_->diameter;
}

void Avatar::set_diameter(float diameter) {
    if (impl_->diameter != diameter) {
        impl_->diameter = diameter;
        queue_layout();
        queue_redraw();
    }
}

SizeRequest Avatar::measure(const Constraints& /*constraints*/) const {
    const float d = impl_->diameter;
    return {d, d, d, d};
}

void Avatar::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float d = std::min(a.width, a.height);
    const float circle_radius = d * 0.5F;

    // Center the circle within the allocation.
    const float cx = a.x + (a.width - d) * 0.5F;
    const float cy = a.y + (a.height - d) * 0.5F;
    const Rect circle_rect{cx, cy, d, d};

    // Background circle.
    ctx.add_rounded_rect(
        circle_rect, theme_color("background", Color{0.75F, 0.78F, 0.82F, 1.0F}), circle_radius);

    if (impl_->image_data != nullptr && impl_->image_width > 0 && impl_->image_height > 0) {
        note_image_snapshot_for_diagnostics();
        ctx.push_rounded_clip(circle_rect, circle_radius);
        ctx.add_image(circle_rect,
                      impl_->image_data,
                      impl_->image_width,
                      impl_->image_height,
                      ScaleMode::NearestNeighbor);
        ctx.pop_container();
    } else if (!impl_->initials.empty()) {
        const auto font = avatar_font(d);
        const auto measured = measure_text(impl_->initials, font);
        const float text_x = cx + std::max(0.0F, (d - measured.width) * 0.5F);
        const float text_y = cy + std::max(0.0F, (d - measured.height) * 0.5F);
        ctx.add_text({text_x, text_y},
                     std::string(impl_->initials),
                     theme_color("text-color", Color{1.0F, 1.0F, 1.0F, 1.0F}),
                     font);
    } else {
        const auto mark = theme_color("person-color", Color{0.45F, 0.48F, 0.52F, 1.0F});
        ctx.push_rounded_clip(circle_rect, circle_radius);
        const float head = d * 0.32F;
        ctx.add_rounded_rect(
            {cx + ((d - head) * 0.5F), cy + (d * 0.18F), head, head}, mark, head * 0.5F);
        const float shoulder_w = d * 0.62F;
        const float shoulder_h = d * 0.36F;
        ctx.add_rounded_rect(
            {cx + ((d - shoulder_w) * 0.5F), cy + (d * 0.52F), shoulder_w, shoulder_h},
            mark,
            shoulder_h * 0.5F);
        ctx.pop_container();
    }

    if (impl_->status != AvatarStatus::None) {
        const float mark = std::max(8.0F, d * 0.28F);
        const float mark_x = circle_rect.right() - (mark * 0.78F);
        const float mark_y = circle_rect.bottom() - (mark * 0.78F);
        const float ring = 2.0F;
        ctx.add_rounded_rect(
            {mark_x - ring, mark_y - ring, mark + (ring * 2.0F), mark + (ring * 2.0F)},
            theme_color("status-ring-color", Color{1.0F, 1.0F, 1.0F, 1.0F}),
            (mark + (ring * 2.0F)) * 0.5F);
        ctx.add_rounded_rect(
            {mark_x, mark_y, mark, mark}, status_color(impl_->status), mark * 0.5F);
    }

    // Optional border.
    ctx.add_border(circle_rect,
                   theme_color("border-color", Color{0.0F, 0.0F, 0.0F, 0.08F}),
                   1.0F,
                   circle_radius);
}

} // namespace nk
