#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/combo_box.h>
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

constexpr int kComboPopupMaxVisible = 6;

struct PopupGeometry {
    Rect bounds;
    float item_height = 0.0F;
    int visible_count = 0;
};

PopupGeometry popup_geometry(Rect field_bounds, std::size_t item_count, float item_height) {
    PopupGeometry geometry;
    geometry.item_height = item_height;
    geometry.visible_count =
        static_cast<int>(std::min<std::size_t>(kComboPopupMaxVisible, item_count));
    geometry.bounds = {
        field_bounds.x,
        field_bounds.bottom() + 4.0F,
        field_bounds.width,
        geometry.visible_count > 0 ? geometry.visible_count * item_height + 2.0F : 0.0F,
    };
    return geometry;
}

// `popup_index_at` returns the absolute item index under `point`, taking the current scroll
// offset into account (or -1 if the pointer is outside the popup). Note that `visible_count`
// clamps the number of rows rendered; `scroll_offset` tracks which item is drawn at row 0.
int popup_index_at(const PopupGeometry& geometry, Point point, int scroll_offset) {
    if (geometry.visible_count <= 0 || !geometry.bounds.contains(point)) {
        return -1;
    }

    const float local_y = point.y - geometry.bounds.y - 1.0F;
    const auto row = static_cast<int>(local_y / geometry.item_height);
    if (row < 0 || row >= geometry.visible_count) {
        return -1;
    }
    return row + scroll_offset;
}

// Clamps a scroll offset so the current highlight remains visible within the popup viewport.
int clamp_scroll_offset(int scroll_offset, int highlight, int item_count, int visible_count) {
    if (item_count <= visible_count || visible_count <= 0) {
        return 0;
    }
    const int max_offset = item_count - visible_count;
    int offset = std::clamp(scroll_offset, 0, max_offset);
    if (highlight >= 0) {
        if (highlight < offset) {
            offset = highlight;
        } else if (highlight >= offset + visible_count) {
            offset = highlight - visible_count + 1;
        }
    }
    return std::clamp(offset, 0, max_offset);
}

void invalidate_popup_frame(Widget& widget) {
    widget.queue_redraw();
}

Rect field_damage_rect(const ComboBox& combo_box) {
    const auto a = combo_box.allocation();
    if (a.width <= 0.0F || a.height <= 0.0F) {
        return {};
    }

    auto body = a;
    if (has_flag(combo_box.state_flags(), StateFlags::Focused)) {
        body = {a.x + 2.0F, a.y + 2.0F, a.width - 4.0F, a.height - 4.0F};
    }

    const Rect inner = {
        body.x + 1.0F,
        body.y + 1.0F,
        std::max(0.0F, body.width - 2.0F),
        std::max(0.0F, body.height - 2.0F),
    };
    return {inner.x - a.x, inner.y - a.y, inner.width, inner.height};
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
    // Index of the first visible item when the popup can't show them all. Updated together with
    // highlighted_index so the highlight is always on screen; see clamp_scroll_offset().
    int popup_scroll_offset = 0;
};

std::shared_ptr<ComboBox> ComboBox::create() {
    return std::shared_ptr<ComboBox>(new ComboBox());
}

ComboBox::ComboBox() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("combo-box");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::List);
}

ComboBox::~ComboBox() = default;

void ComboBox::set_items(std::vector<std::string> items) {
    impl_->items = std::move(items);
    impl_->selected_index = -1;
    // Any open popup state refers to the previous item list; reset it so the next open rebuilds
    // from a known baseline and the current highlight can't index past the new end.
    impl_->popup_open = false;
    impl_->armed = false;
    impl_->armed_index = -1;
    impl_->highlighted_index = -1;
    impl_->popup_scroll_offset = 0;
    ensure_accessible().set_name({});
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
        throw std::out_of_range("ComboBox::set_selected_index: index out of range");
    }
    if (impl_->selected_index != index) {
        impl_->selected_index = index;
        ensure_accessible().set_name(std::string(selected_text()));
        const auto damage = field_damage_rect(*this);
        if (damage.width <= 0.0F || damage.height <= 0.0F) {
            queue_redraw();
        } else {
            queue_redraw(damage);
        }
        impl_->selection_changed.emit(index);
    }
}

std::string_view ComboBox::selected_text() const {
    if (impl_->selected_index < 0 ||
        impl_->selected_index >= static_cast<int>(impl_->items.size())) {
        return {};
    }
    return impl_->items[static_cast<std::size_t>(impl_->selected_index)];
}

Signal<int>& ComboBox::on_selection_changed() {
    return impl_->selection_changed;
}

SizeRequest ComboBox::measure(const Constraints& /*constraints*/) const {
    const float w = theme_number("min-width", 240.0F);
    const float h = theme_number("min-height", 36.0F);
    return {120.0F, h, w, h};
}

bool ComboBox::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1 || impl_->items.empty()) {
        return false;
    }

    const auto popup =
        popup_geometry(allocation(), impl_->items.size(), theme_number("popup-item-height", 28.0F));
    const auto point = Point{event.x, event.y};
    const int item_count = static_cast<int>(impl_->items.size());

    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->armed = allocation().contains(point);
        impl_->armed_index = impl_->popup_open
            ? popup_index_at(popup, point, impl_->popup_scroll_offset)
            : -1;
        return impl_->armed || impl_->armed_index >= 0;
    case MouseEvent::Type::Release: {
        const bool release_on_field = allocation().contains(point);
        const auto release_index = popup_index_at(popup, point, impl_->popup_scroll_offset);
        bool consumed = false;

        if (impl_->armed_index >= 0) {
            if (release_index == impl_->armed_index) {
                set_selected_index(release_index);
                consumed = true;
            }
            preserve_damage_regions_for_next_redraw();
            impl_->popup_open = false;
            impl_->highlighted_index = -1;
            impl_->armed_index = -1;
            impl_->armed = false;
            impl_->popup_scroll_offset = 0;
            invalidate_popup_frame(*this);
            return consumed;
        }

        const bool activate = impl_->armed && release_on_field;
        impl_->armed = false;
        impl_->armed_index = -1;
        if (!activate) {
            return false;
        }

        preserve_damage_regions_for_next_redraw();
        impl_->popup_open = !impl_->popup_open;
        impl_->highlighted_index = impl_->popup_open ? std::max(0, impl_->selected_index) : -1;
        impl_->popup_scroll_offset = clamp_scroll_offset(
            impl_->popup_scroll_offset, impl_->highlighted_index, item_count, popup.visible_count);
        invalidate_popup_frame(*this);
        return true;
    }
    case MouseEvent::Type::Move:
        if (impl_->popup_open) {
            const int next_highlight = popup_index_at(popup, point, impl_->popup_scroll_offset);
            if (impl_->highlighted_index != next_highlight) {
                impl_->highlighted_index = next_highlight;
                invalidate_popup_frame(*this);
            }
        }
        return impl_->popup_open && popup.bounds.contains(point);
    case MouseEvent::Type::Enter:
        return false;
    case MouseEvent::Type::Leave:
        if (impl_->popup_open && impl_->highlighted_index != -1) {
            impl_->highlighted_index = -1;
            invalidate_popup_frame(*this);
        }
        return false;
    case MouseEvent::Type::Scroll:
        if (!impl_->popup_open) {
            return false;
        }
        if (event.scroll_dy > 0.0F && impl_->highlighted_index > 0) {
            --impl_->highlighted_index;
            impl_->popup_scroll_offset = clamp_scroll_offset(impl_->popup_scroll_offset,
                                                             impl_->highlighted_index,
                                                             item_count,
                                                             popup.visible_count);
            invalidate_popup_frame(*this);
            return true;
        }
        if (event.scroll_dy < 0.0F && impl_->highlighted_index + 1 < item_count) {
            ++impl_->highlighted_index;
            impl_->popup_scroll_offset = clamp_scroll_offset(impl_->popup_scroll_offset,
                                                             impl_->highlighted_index,
                                                             item_count,
                                                             popup.visible_count);
            invalidate_popup_frame(*this);
            return true;
        }
        return false;
    }

    return false;
}

bool ComboBox::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press || impl_->items.empty()) {
        return false;
    }

    const auto last_index = static_cast<int>(impl_->items.size()) - 1;
    const auto popup_visible_count = popup_geometry(allocation(),
                                                    impl_->items.size(),
                                                    theme_number("popup-item-height", 28.0F))
                                         .visible_count;
    auto item_count = static_cast<int>(impl_->items.size());
    auto update_scroll = [&] {
        impl_->popup_scroll_offset = clamp_scroll_offset(impl_->popup_scroll_offset,
                                                          impl_->highlighted_index,
                                                          item_count,
                                                          popup_visible_count);
    };
    auto open_popup = [&, this](int highlight) {
        preserve_damage_regions_for_next_redraw();
        impl_->popup_open = true;
        impl_->highlighted_index = std::clamp(highlight, 0, last_index);
        update_scroll();
        invalidate_popup_frame(*this);
    };

    if (impl_->popup_open) {
        switch (event.key) {
        case KeyCode::Escape:
            preserve_damage_regions_for_next_redraw();
            impl_->popup_open = false;
            impl_->highlighted_index = -1;
            impl_->popup_scroll_offset = 0;
            invalidate_popup_frame(*this);
            return true;
        case KeyCode::Up: {
            if (impl_->highlighted_index < 0) {
                impl_->highlighted_index = std::max(0, impl_->selected_index);
            } else if (impl_->highlighted_index > 0) {
                --impl_->highlighted_index;
            }
            update_scroll();
            invalidate_popup_frame(*this);
            return true;
        }
        case KeyCode::Down: {
            if (impl_->highlighted_index < 0) {
                impl_->highlighted_index = std::max(0, impl_->selected_index);
            } else if (impl_->highlighted_index < last_index) {
                ++impl_->highlighted_index;
            }
            update_scroll();
            invalidate_popup_frame(*this);
            return true;
        }
        case KeyCode::Home:
            impl_->highlighted_index = 0;
            update_scroll();
            invalidate_popup_frame(*this);
            return true;
        case KeyCode::End:
            impl_->highlighted_index = last_index;
            update_scroll();
            invalidate_popup_frame(*this);
            return true;
        case KeyCode::Return:
        case KeyCode::Space: {
            if (impl_->highlighted_index >= 0) {
                set_selected_index(impl_->highlighted_index);
            }
            preserve_damage_regions_for_next_redraw();
            impl_->popup_open = false;
            impl_->highlighted_index = -1;
            impl_->popup_scroll_offset = 0;
            invalidate_popup_frame(*this);
            return true;
        }
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
            set_selected_index((impl_->selected_index + last_index) %
                               static_cast<int>(impl_->items.size()));
        }
        return true;
    case KeyCode::Right:
        if (impl_->selected_index < 0) {
            set_selected_index(0);
        } else {
            set_selected_index((impl_->selected_index + 1) % static_cast<int>(impl_->items.size()));
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

    const auto popup =
        popup_geometry(allocation(), impl_->items.size(), theme_number("popup-item-height", 28.0F));
    return popup.bounds.contains(point);
}

std::vector<Rect> ComboBox::damage_regions() const {
    if (!impl_->popup_open || impl_->items.empty()) {
        return Widget::damage_regions();
    }

    auto regions = std::vector<Rect>{allocation()};
    const auto popup =
        popup_geometry(allocation(), impl_->items.size(), theme_number("popup-item-height", 28.0F));
    if (popup.visible_count > 0) {
        regions.push_back(popup.bounds);
    }
    return regions;
}

CursorShape ComboBox::cursor_shape() const {
    return CursorShape::PointingHand;
}

void ComboBox::on_focus_changed(bool focused) {
    if (!focused && impl_->popup_open) {
        preserve_damage_regions_for_next_redraw();
        impl_->popup_open = false;
        impl_->armed = false;
        impl_->armed_index = -1;
        impl_->highlighted_index = -1;
        impl_->popup_scroll_offset = 0;
        invalidate_popup_frame(*this);
    }
}

void ComboBox::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 10.0F);
    const float popup_radius = theme_number("popup-radius", 12.0F);
    const float selection_radius = theme_number("selection-radius", 8.0F);
    auto body = a;

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a,
                             theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
                             corner_radius + 2.0F);
        body = {a.x + 2.0F, a.y + 2.0F, a.width - 4.0F, a.height - 4.0F};
    }

    ctx.add_rounded_rect(
        body, theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}), 1.0F, corner_radius);
    Rect inner = {body.x + 1.0F,
                  body.y + 1.0F,
                  std::max(0.0F, body.width - 2.0F),
                  std::max(0.0F, body.height - 2.0F)};

    const float arrow_width = 28.0F;
    ctx.add_color_rect({inner.right() - arrow_width, inner.y, 1.0F, inner.height},
                       theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}));

    const auto font = combo_box_font();
    auto text = selected_text();
    if (!text.empty()) {
        const auto measured = measure_text(text, font);
        const float text_y = inner.y + std::max(0.0F, (inner.height - measured.height) * 0.5F);
        ctx.add_text({inner.x + 12.0F, text_y},
                     std::string(text),
                     theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F}),
                     font);
    }

    const auto chevron_font = FontDescriptor{
        .family = {},
        .size = 13.0F,
        .weight = FontWeight::Medium,
    };
    const auto chevron_text = impl_->popup_open ? "^" : "v";
    const auto chevron_size = measure_text(chevron_text, chevron_font);
    const float arrow_x =
        inner.right() - ((arrow_width - chevron_size.width) * 0.5F) - chevron_size.width;
    const float arrow_y = inner.y + std::max(0.0F, (inner.height - chevron_size.height) * 0.5F);
    const auto arrow_color = theme_color("chevron-color", Color{0.45F, 0.48F, 0.54F, 1.0F});
    ctx.add_text({arrow_x, arrow_y}, chevron_text, arrow_color, chevron_font);

    if (!impl_->popup_open || impl_->items.empty()) {
        return;
    }

    const auto popup =
        popup_geometry(allocation(), impl_->items.size(), theme_number("popup-item-height", 28.0F));
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
        theme_color("popup-background", theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F})),
        popup_radius);
    ctx.add_border(popup.bounds,
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

    const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    const auto hover_bg = theme_color("popup-hover-background", Color{0.94F, 0.95F, 0.97F, 1.0F});
    const auto selected_bg =
        theme_color("popup-selected-background", Color{0.86F, 0.92F, 0.99F, 1.0F});

    float row_y = popup_inner.y;
    const int item_count = static_cast<int>(impl_->items.size());
    const int effective_scroll = clamp_scroll_offset(
        impl_->popup_scroll_offset, impl_->highlighted_index, item_count, popup.visible_count);
    for (int row = 0; row < popup.visible_count; ++row) {
        const int index = row + effective_scroll;
        if (index < 0 || index >= item_count) {
            row_y += popup.item_height;
            continue;
        }
        const bool selected = index == impl_->selected_index;
        const bool highlighted = index == impl_->highlighted_index;
        Color row_bg = selected ? selected_bg : Color{};
        if (highlighted) {
            row_bg = hover_bg;
        }
        if (selected || highlighted) {
            ctx.add_rounded_rect({popup_inner.x + 4.0F,
                                  row_y + 2.0F,
                                  popup_inner.width - 8.0F,
                                  popup.item_height - 4.0F},
                                 row_bg,
                                 selection_radius);
        }

        const auto item_text = impl_->items[static_cast<std::size_t>(index)];
        const auto item_size = measure_text(item_text, font);
        const float text_y = row_y + std::max(0.0F, (popup.item_height - item_size.height) * 0.5F);
        ctx.add_text({popup_inner.x + 12.0F, text_y}, item_text, text_color, font);
        row_y += popup.item_height;
    }
    ctx.pop_container();
}

} // namespace nk
