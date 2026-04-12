#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/check_box.h>

namespace nk {

namespace {

FontDescriptor check_box_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

} // namespace

struct CheckBox::Impl {
    std::string label;
    Signal<bool> toggled;
    bool checked = false;
};

std::shared_ptr<CheckBox> CheckBox::create(std::string label) {
    return std::shared_ptr<CheckBox>(new CheckBox(std::move(label)));
}

CheckBox::CheckBox(std::string label) : impl_(std::make_unique<Impl>()) {
    impl_->label = std::move(label);
    set_focusable(true);
    add_style_class("check-box");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::CheckBox);
    accessible.set_name(impl_->label);
    accessible.add_action(AccessibleAction::Toggle, [this]() {
        set_checked(!impl_->checked);
        return true;
    });
}

CheckBox::~CheckBox() = default;

std::string_view CheckBox::label() const {
    return impl_->label;
}

void CheckBox::set_label(std::string label) {
    if (impl_->label != label) {
        impl_->label = std::move(label);
        ensure_accessible().set_name(impl_->label);
        queue_layout();
        queue_redraw();
    }
}

bool CheckBox::is_checked() const {
    return impl_->checked;
}

void CheckBox::set_checked(bool checked) {
    if (impl_->checked != checked) {
        impl_->checked = checked;
        set_state_flag(StateFlags::Checked, checked);
        ensure_accessible().set_value(checked ? "true" : "false");
        impl_->toggled.emit(checked);
        queue_redraw();
    }
}

Signal<bool>& CheckBox::on_toggled() {
    return impl_->toggled;
}

SizeRequest CheckBox::measure(const Constraints& /*constraints*/) const {
    const float box_size = theme_number("box-size", 16.0F);
    const float gap = theme_number("gap", 8.0F);
    const float min_height = theme_number("min-height", 24.0F);
    const auto measured = measure_text(impl_->label, check_box_font());
    const float w = box_size + gap + measured.width;
    const float h = std::max(min_height, std::max(box_size, measured.height));
    return {w, h, w, h};
}

bool CheckBox::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return false;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        return allocation().contains({event.x, event.y});
    case MouseEvent::Type::Release:
        if (allocation().contains({event.x, event.y})) {
            set_checked(!impl_->checked);
            return true;
        }
        return false;
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool CheckBox::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Space || event.key == KeyCode::Return) {
        set_checked(!impl_->checked);
        return true;
    }

    return false;
}

CursorShape CheckBox::cursor_shape() const {
    return CursorShape::PointingHand;
}

void CheckBox::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float box_size = theme_number("box-size", 16.0F);
    const float gap = theme_number("gap", 8.0F);
    const float corner_radius = theme_number("corner-radius", 3.0F);

    // Center the box vertically.
    const float box_y = a.y + std::max(0.0F, (a.height - box_size) * 0.5F);
    const Rect box_rect{a.x, box_y, box_size, box_size};

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(
            {box_rect.x - 2.0F, box_rect.y - 2.0F, box_rect.width + 4.0F, box_rect.height + 4.0F},
            theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
            corner_radius + 2.0F);
    }

    // Draw the check box frame.
    const auto box_bg = impl_->checked
        ? theme_color("checked-background", Color{0.2F, 0.45F, 0.85F, 1.0F})
        : theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F});
    ctx.add_rounded_rect(box_rect, box_bg, corner_radius);
    ctx.add_border(box_rect,
                   theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F}),
                   1.0F, corner_radius);

    // Draw check mark when checked (two strokes forming a checkmark shape).
    if (impl_->checked) {
        const auto check_color = theme_color("check-color", Color{1.0F, 1.0F, 1.0F, 1.0F});
        // Short leg of check: from lower-left area toward center-bottom.
        const float cx = box_rect.x + box_size * 0.5F;
        const float cy = box_rect.y + box_size * 0.5F;
        // Approximate check mark with two small rectangles rotated conceptually.
        // Short stroke: bottom-left to center-bottom.
        ctx.add_color_rect({cx - 4.0F, cy + 0.0F, 5.0F, 2.0F}, check_color);
        // Long stroke: center-bottom to upper-right.
        ctx.add_color_rect({cx - 1.0F, cy - 3.0F, 2.0F, 5.0F}, check_color);
    }

    // Draw label text.
    const auto font = check_box_font();
    const auto measured = measure_text(impl_->label, font);
    const float text_x = a.x + box_size + gap;
    const float text_y = a.y + std::max(0.0F, (a.height - measured.height) * 0.5F);
    ctx.add_text({text_x, text_y},
                 std::string(impl_->label),
                 theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F}),
                 font);
}

} // namespace nk
