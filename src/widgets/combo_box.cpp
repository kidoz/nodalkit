#include <nk/widgets/combo_box.h>

#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>

#include <algorithm>
#include <stdexcept>

namespace nk {

namespace {

FontDescriptor combo_box_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

struct PopupGeometry {
    Rect bounds;
    float item_height = 0.0F;
    int visible_count = 0;
};

PopupGeometry popup_geometry(
    Rect field_bounds,
    std::size_t item_count,
    float item_height) {
    PopupGeometry geometry;
    geometry.item_height = item_height;
    geometry.visible_count = static_cast<int>(std::min<std::size_t>(6, item_count));
    geometry.bounds = {
        field_bounds.x,
        field_bounds.bottom() + 4.0F,
        field_bounds.width,
        geometry.visible_count > 0
            ? geometry.visible_count * item_height + 2.0F
            : 0.0F,
    };
    return geometry;
}

int popup_index_at(PopupGeometry const& geometry, Point point) {
    if (geometry.visible_count <= 0 || !geometry.bounds.contains(point)) {
        return -1;
    }

    float const local_y = point.y - geometry.bounds.y - 1.0F;
    auto const index = static_cast<int>(local_y / geometry.item_height);
    if (index < 0 || index >= geometry.visible_count) {
        return -1;
    }
    return index;
}

} // namespace

struct ComboBox::Impl {
    std::vector<std::string> items;
    int selected_index = -1;
    Signal<int> selection_changed;
    bool armed = false;
    bool popup_open = false;
    int armed_index = -1;
    int highlighted_index = -1;
};

std::shared_ptr<ComboBox> ComboBox::create() {
    return std::shared_ptr<ComboBox>(new ComboBox());
}

ComboBox::ComboBox()
    : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("combo-box");
}

ComboBox::~ComboBox() = default;

void ComboBox::set_items(std::vector<std::string> items) {
    impl_->items = std::move(items);
    impl_->selected_index = -1;
    queue_layout();
    queue_redraw();
}

std::size_t ComboBox::item_count() const {
    return impl_->items.size();
}

std::string_view ComboBox::item(std::size_t index) const {
    if (index >= impl_->items.size()) {
        throw std::out_of_range("ComboBox::item: index out of range");
    }
    return impl_->items[index];
}

int ComboBox::selected_index() const {
    return impl_->selected_index;
}

void ComboBox::set_selected_index(int index) {
    if (index >= static_cast<int>(impl_->items.size())) {
        throw std::out_of_range(
            "ComboBox::set_selected_index: index out of range");
    }
    if (impl_->selected_index != index) {
        impl_->selected_index = index;
        queue_redraw();
        impl_->selection_changed.emit(index);
    }
}

std::string_view ComboBox::selected_text() const {
    if (impl_->selected_index < 0 ||
        impl_->selected_index >=
            static_cast<int>(impl_->items.size())) {
        return {};
    }
    return impl_->items[static_cast<std::size_t>(impl_->selected_index)];
}

Signal<int>& ComboBox::on_selection_changed() {
    return impl_->selection_changed;
}

SizeRequest ComboBox::measure(Constraints const& /*constraints*/) const {
    float const w = theme_number("min-width", 240.0F);
    float const h = theme_number("min-height", 36.0F);
    return {120.0F, h, w, h};
}

bool ComboBox::handle_mouse_event(MouseEvent const& event) {
    if (event.button != 1 || impl_->items.empty()) {
        return false;
    }

    auto const popup = popup_geometry(
        allocation(),
        impl_->items.size(),
        theme_number("popup-item-height", 28.0F));
    auto const point = Point{event.x, event.y};

    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->armed = allocation().contains(point);
        impl_->armed_index = impl_->popup_open ? popup_index_at(popup, point) : -1;
        return impl_->armed || impl_->armed_index >= 0;
    case MouseEvent::Type::Release: {
        bool const release_on_field = allocation().contains(point);
        auto const release_index = popup_index_at(popup, point);
        bool consumed = false;

        if (impl_->armed_index >= 0) {
            if (release_index == impl_->armed_index) {
                set_selected_index(release_index);
                consumed = true;
            }
            impl_->popup_open = false;
            impl_->highlighted_index = -1;
            impl_->armed_index = -1;
            impl_->armed = false;
            queue_redraw();
            return consumed;
        }

        bool const activate = impl_->armed && release_on_field;
        impl_->armed = false;
        impl_->armed_index = -1;
        if (!activate) {
            return false;
        }

        impl_->popup_open = !impl_->popup_open;
        impl_->highlighted_index = impl_->popup_open
            ? std::max(0, impl_->selected_index)
            : -1;
        queue_redraw();
        return true;
    }
    case MouseEvent::Type::Move:
        if (impl_->popup_open) {
            int const next_highlight = popup_index_at(popup, point);
            if (impl_->highlighted_index != next_highlight) {
                impl_->highlighted_index = next_highlight;
                queue_redraw();
            }
        }
        return impl_->popup_open && popup.bounds.contains(point);
    case MouseEvent::Type::Enter:
        return false;
    case MouseEvent::Type::Leave:
        if (impl_->popup_open && impl_->highlighted_index != -1) {
            impl_->highlighted_index = -1;
            queue_redraw();
        }
        return false;
    case MouseEvent::Type::Scroll:
        if (!impl_->popup_open) {
            return false;
        }
        if (event.scroll_dy > 0.0F && impl_->highlighted_index > 0) {
            --impl_->highlighted_index;
            queue_redraw();
            return true;
        }
        if (event.scroll_dy < 0.0F
            && impl_->highlighted_index + 1 < static_cast<int>(impl_->items.size())) {
            ++impl_->highlighted_index;
            queue_redraw();
            return true;
        }
        return false;
    }

    return false;
}

bool ComboBox::handle_key_event(KeyEvent const& event) {
    if (event.type != KeyEvent::Type::Press || impl_->items.empty()) {
        return false;
    }

    auto const last_index = static_cast<int>(impl_->items.size()) - 1;
    auto open_popup = [this](int highlight) {
        impl_->popup_open = true;
        impl_->highlighted_index = std::clamp(highlight, 0,
                                              static_cast<int>(impl_->items.size()) - 1);
        queue_redraw();
    };

    if (impl_->popup_open) {
        switch (event.key) {
        case KeyCode::Escape:
            impl_->popup_open = false;
            impl_->highlighted_index = -1;
            queue_redraw();
            return true;
        case KeyCode::Up:
            if (impl_->highlighted_index < 0) {
                impl_->highlighted_index = std::max(0, impl_->selected_index);
            } else if (impl_->highlighted_index > 0) {
                --impl_->highlighted_index;
            }
            queue_redraw();
            return true;
        case KeyCode::Down:
            if (impl_->highlighted_index < 0) {
                impl_->highlighted_index = std::max(0, impl_->selected_index);
            } else if (impl_->highlighted_index < last_index) {
                ++impl_->highlighted_index;
            }
            queue_redraw();
            return true;
        case KeyCode::Return:
        case KeyCode::Space:
            if (impl_->highlighted_index >= 0) {
                set_selected_index(impl_->highlighted_index);
            }
            impl_->popup_open = false;
            impl_->highlighted_index = -1;
            queue_redraw();
            return true;
        default:
            return false;
        }
    }

    switch (event.key) {
    case KeyCode::Up:
        open_popup(impl_->selected_index >= 0 ? impl_->selected_index : last_index);
        return true;
    case KeyCode::Down:
    case KeyCode::Space:
    case KeyCode::Return:
        open_popup(std::max(0, impl_->selected_index));
        return true;
    case KeyCode::Left:
        if (impl_->selected_index < 0) {
            set_selected_index(last_index);
        } else {
            set_selected_index(
                (impl_->selected_index + last_index)
                % static_cast<int>(impl_->items.size()));
        }
        return true;
    case KeyCode::Right:
        if (impl_->selected_index < 0) {
            set_selected_index(0);
        } else {
            set_selected_index(
                (impl_->selected_index + 1)
                % static_cast<int>(impl_->items.size()));
        }
        return true;
    default:
        return false;
    }
}

bool ComboBox::hit_test(Point point) const {
    if (Widget::hit_test(point)) {
        return true;
    }

    if (!impl_->popup_open) {
        return false;
    }

    auto const popup = popup_geometry(
        allocation(),
        impl_->items.size(),
        theme_number("popup-item-height", 28.0F));
    return popup.bounds.contains(point);
}

void ComboBox::on_focus_changed(bool focused) {
    if (!focused && impl_->popup_open) {
        impl_->popup_open = false;
        impl_->armed = false;
        impl_->armed_index = -1;
        impl_->highlighted_index = -1;
        queue_redraw();
    }
}

void ComboBox::snapshot(SnapshotContext& ctx) const {
    auto const a = allocation();
    float const corner_radius = theme_number("corner-radius", 10.0F);
    float const popup_radius = theme_number("popup-radius", 12.0F);
    float const selection_radius = theme_number("selection-radius", 8.0F);
    auto body = a;

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(
            a,
            theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
            corner_radius + 2.0F);
        body = {a.x + 2.0F, a.y + 2.0F, a.width - 4.0F, a.height - 4.0F};
    }

    ctx.add_rounded_rect(
        body,
        theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}),
        corner_radius);
    ctx.add_border(
        body,
        theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}),
        1.0F,
        corner_radius);
    Rect inner = {body.x + 1.0F, body.y + 1.0F,
                  std::max(0.0F, body.width - 2.0F),
                  std::max(0.0F, body.height - 2.0F)};

    float const arrow_width = 28.0F;
    ctx.add_color_rect({inner.right() - arrow_width, inner.y, 1.0F, inner.height},
                       theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}));

    auto const font = combo_box_font();
    auto text = selected_text();
    if (!text.empty()) {
        auto const measured = measure_text(text, font);
        float const text_y =
            inner.y + std::max(0.0F, (inner.height - measured.height) * 0.5F);
        ctx.add_text(
            {inner.x + 12.0F, text_y},
            std::string(text),
            theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F}),
            font);
    }

    auto const chevron_font = FontDescriptor{
        .family = {},
        .size = 13.0F,
        .weight = FontWeight::Medium,
    };
    auto const chevron_text = impl_->popup_open ? "^" : "v";
    auto const chevron_size = measure_text(chevron_text, chevron_font);
    float const arrow_x =
        inner.right() - ((arrow_width - chevron_size.width) * 0.5F)
        - chevron_size.width;
    float const arrow_y =
        inner.y + std::max(0.0F, (inner.height - chevron_size.height) * 0.5F);
    auto const arrow_color =
        theme_color("chevron-color", Color{0.45F, 0.48F, 0.54F, 1.0F});
    ctx.add_text({arrow_x, arrow_y}, chevron_text, arrow_color, chevron_font);

    if (!impl_->popup_open || impl_->items.empty()) {
        return;
    }

    auto const popup = popup_geometry(
        allocation(),
        impl_->items.size(),
        theme_number("popup-item-height", 28.0F));
    if (popup.visible_count <= 0) {
        return;
    }

    ctx.push_overlay_container(popup.bounds);
    ctx.add_rounded_rect(
        {popup.bounds.x, popup.bounds.y + 1.0F, popup.bounds.width, popup.bounds.height},
        Color{0.08F, 0.12F, 0.18F, 0.05F},
        popup_radius + 1.0F);
    ctx.add_rounded_rect(
        popup.bounds,
        theme_color("popup-background",
                    theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F})),
        popup_radius);
    ctx.add_border(
        popup.bounds,
        theme_color("popup-border-color",
                    theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F})),
        1.0F,
        popup_radius);
    Rect popup_inner = {
        popup.bounds.x + 1.0F,
        popup.bounds.y + 1.0F,
        std::max(0.0F, popup.bounds.width - 2.0F),
        std::max(0.0F, popup.bounds.height - 2.0F),
    };

    auto const text_color =
        theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    auto const hover_bg =
        theme_color("popup-hover-background", Color{0.94F, 0.95F, 0.97F, 1.0F});
    auto const selected_bg =
        theme_color("popup-selected-background", Color{0.86F, 0.92F, 0.99F, 1.0F});

    float row_y = popup_inner.y;
    for (int index = 0; index < popup.visible_count; ++index) {
        bool const selected = index == impl_->selected_index;
        bool const highlighted = index == impl_->highlighted_index;
        Color row_bg = selected ? selected_bg : Color{};
        if (highlighted) {
            row_bg = hover_bg;
        }
        if (selected || highlighted) {
            ctx.add_rounded_rect(
                {popup_inner.x + 4.0F, row_y + 2.0F,
                 popup_inner.width - 8.0F, popup.item_height - 4.0F},
                row_bg,
                selection_radius);
        }

        auto const item_text = impl_->items[index];
        auto const item_size = measure_text(item_text, font);
        float const text_y =
            row_y + std::max(0.0F, (popup.item_height - item_size.height) * 0.5F);
        ctx.add_text(
            {popup_inner.x + 12.0F, text_y},
            item_text,
            text_color,
            font);
        row_y += popup.item_height;
    }
    ctx.pop_container();
}

} // namespace nk
