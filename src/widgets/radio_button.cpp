#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/radio_button.h>

namespace nk {

namespace {

FontDescriptor radio_button_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

} // namespace

// ---- RadioGroup ----

struct RadioGroup::Impl {
    std::vector<RadioButton*> members;
};

RadioGroup::RadioGroup() : impl_(std::make_unique<Impl>()) {}

RadioGroup::~RadioGroup() = default;

std::shared_ptr<RadioGroup> RadioGroup::create() {
    return std::make_shared<RadioGroup>();
}

void RadioGroup::add(RadioButton* button) {
    impl_->members.push_back(button);
}

void RadioGroup::remove(RadioButton* button) {
    auto& m = impl_->members;
    m.erase(std::remove(m.begin(), m.end(), button), m.end());
}

void RadioGroup::select(RadioButton* button) {
    for (auto* member : impl_->members) {
        member->set_selected(member == button);
    }
}

// ---- RadioButton ----

struct RadioButton::Impl {
    std::string label;
    Signal<> selected_signal;
    bool is_selected = false;
    std::shared_ptr<RadioGroup> group;
};

std::shared_ptr<RadioButton> RadioButton::create(std::string label) {
    return std::shared_ptr<RadioButton>(new RadioButton(std::move(label)));
}

RadioButton::RadioButton(std::string label) : impl_(std::make_unique<Impl>()) {
    impl_->label = std::move(label);
    set_focusable(true);
    add_style_class("radio-button");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::RadioButton);
    accessible.set_name(impl_->label);
    accessible.add_action(AccessibleAction::Activate, [this]() {
        if (impl_->group) {
            impl_->group->select(this);
        } else {
            set_selected(true);
        }
        return true;
    });
}

RadioButton::~RadioButton() {
    if (impl_->group) {
        impl_->group->remove(this);
    }
}

std::string_view RadioButton::label() const {
    return impl_->label;
}

void RadioButton::set_label(std::string label) {
    if (impl_->label != label) {
        impl_->label = std::move(label);
        ensure_accessible().set_name(impl_->label);
        queue_layout();
        queue_redraw();
    }
}

bool RadioButton::is_selected() const {
    return impl_->is_selected;
}

void RadioButton::set_selected(bool selected) {
    if (impl_->is_selected != selected) {
        impl_->is_selected = selected;
        set_state_flag(StateFlags::Checked, selected);
        ensure_accessible().set_value(selected ? "true" : "false");
        if (selected) {
            impl_->selected_signal.emit();
        }
        queue_redraw();
    }
}

void RadioButton::set_group(std::shared_ptr<RadioGroup> group) {
    if (impl_->group) {
        impl_->group->remove(this);
    }
    impl_->group = std::move(group);
    if (impl_->group) {
        impl_->group->add(this);
    }
}

std::shared_ptr<RadioGroup> RadioButton::group() const {
    return impl_->group;
}

Signal<>& RadioButton::on_selected() {
    return impl_->selected_signal;
}

SizeRequest RadioButton::measure(const Constraints& /*constraints*/) const {
    const float circle_size = theme_number("circle-size", 16.0F);
    const float gap = theme_number("gap", 8.0F);
    const float min_height = theme_number("min-height", 24.0F);
    const auto measured = measure_text(impl_->label, radio_button_font());
    const float w = circle_size + gap + measured.width;
    const float h = std::max(min_height, std::max(circle_size, measured.height));
    return {w, h, w, h};
}

bool RadioButton::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return false;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        return allocation().contains({event.x, event.y});
    case MouseEvent::Type::Release:
        if (allocation().contains({event.x, event.y})) {
            if (impl_->group) {
                impl_->group->select(this);
            } else {
                set_selected(true);
            }
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

bool RadioButton::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Space || event.key == KeyCode::Return) {
        if (impl_->group) {
            impl_->group->select(this);
        } else {
            set_selected(true);
        }
        return true;
    }

    return false;
}

CursorShape RadioButton::cursor_shape() const {
    return CursorShape::PointingHand;
}

void RadioButton::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float circle_size = theme_number("circle-size", 16.0F);
    const float gap = theme_number("gap", 8.0F);
    const float radius = circle_size * 0.5F;

    // Center the circle vertically.
    const float circle_y = a.y + std::max(0.0F, (a.height - circle_size) * 0.5F);
    const Rect circle_rect{a.x, circle_y, circle_size, circle_size};

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(
            {circle_rect.x - 2.0F, circle_rect.y - 2.0F,
             circle_rect.width + 4.0F, circle_rect.height + 4.0F},
            theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
            radius + 2.0F);
    }

    // Outer circle.
    ctx.add_rounded_rect(circle_rect,
                         theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}),
                         radius);
    ctx.add_border(circle_rect,
                   theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F}),
                   1.0F, radius);

    // Inner dot when selected.
    if (impl_->is_selected) {
        const float inner_size = theme_number("inner-dot-size", 8.0F);
        const float inner_offset = (circle_size - inner_size) * 0.5F;
        const Rect inner_rect{
            circle_rect.x + inner_offset,
            circle_rect.y + inner_offset,
            inner_size,
            inner_size};
        ctx.add_rounded_rect(inner_rect,
                             theme_color("selected-color", Color{0.2F, 0.45F, 0.85F, 1.0F}),
                             inner_size * 0.5F);
    }

    // Label text.
    const auto font = radio_button_font();
    const auto measured = measure_text(impl_->label, font);
    const float text_x = a.x + circle_size + gap;
    const float text_y = a.y + std::max(0.0F, (a.height - measured.height) * 0.5F);
    ctx.add_text({text_x, text_y},
                 std::string(impl_->label),
                 theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F}),
                 font);
}

} // namespace nk
