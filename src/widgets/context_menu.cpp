#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/context_menu.h>

namespace nk {

namespace {

FontDescriptor context_menu_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

struct MenuItemEntry {
    std::string label;
    bool is_separator = false;
};

struct PopupGeometry {
    Rect bounds;
    float item_height = 28.0F;
    float separator_height = 9.0F;
};

template <typename MeasureTextFn>
PopupGeometry compute_popup_geometry(Point position,
                                     const std::vector<MenuItemEntry>& items,
                                     MeasureTextFn&& measure_text_fn) {
    PopupGeometry geometry;
    geometry.item_height = 28.0F;
    geometry.separator_height = 9.0F;

    float width = 160.0F;
    float height = 2.0F; // padding
    for (const auto& item : items) {
        if (item.is_separator) {
            height += geometry.separator_height;
        } else {
            const auto measured = measure_text_fn(item.label);
            width = std::max(width, measured.width + 28.0F);
            height += geometry.item_height;
        }
    }

    geometry.bounds = {position.x, position.y, width, height};
    return geometry;
}

int item_index_at(const PopupGeometry& geometry,
                  const std::vector<MenuItemEntry>& items,
                  Point point) {
    if (!geometry.bounds.contains(point)) {
        return -1;
    }

    float y = geometry.bounds.y + 1.0F;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const float row_height =
            items[i].is_separator ? geometry.separator_height : geometry.item_height;
        if (point.y >= y && point.y < y + row_height) {
            if (items[i].is_separator) {
                return -1;
            }
            return static_cast<int>(i);
        }
        y += row_height;
    }
    return -1;
}

} // namespace

struct ContextMenu::Impl {
    std::vector<MenuItemEntry> items;
    Signal<int> item_activated;
    bool open = false;
    Point position;
    int hovered_index = -1;
    int armed_index = -1;
};

std::shared_ptr<ContextMenu> ContextMenu::create() {
    return std::shared_ptr<ContextMenu>(new ContextMenu());
}

ContextMenu::ContextMenu() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("context-menu");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Menu);
    accessible.set_name("context menu");
}

ContextMenu::~ContextMenu() = default;

void ContextMenu::add_item(std::string label) {
    impl_->items.push_back({std::move(label), false});
}

void ContextMenu::add_separator() {
    impl_->items.push_back({"", true});
}

void ContextMenu::clear() {
    impl_->items.clear();
    impl_->hovered_index = -1;
    impl_->armed_index = -1;
}

std::size_t ContextMenu::item_count() const {
    return impl_->items.size();
}

void ContextMenu::show_at(Point position) {
    preserve_damage_regions_for_next_redraw();
    impl_->position = position;
    impl_->open = true;
    impl_->hovered_index = -1;
    impl_->armed_index = -1;
    queue_redraw();
}

void ContextMenu::dismiss() {
    if (!impl_->open) {
        return;
    }
    preserve_damage_regions_for_next_redraw();
    impl_->open = false;
    impl_->hovered_index = -1;
    impl_->armed_index = -1;
    queue_redraw();
}

bool ContextMenu::is_open() const {
    return impl_->open;
}

Signal<int>& ContextMenu::on_item_activated() {
    return impl_->item_activated;
}

SizeRequest ContextMenu::measure(const Constraints& /*constraints*/) const {
    return {0.0F, 0.0F, 0.0F, 0.0F};
}

bool ContextMenu::hit_test(Point point) const {
    if (Widget::hit_test(point)) {
        return true;
    }

    if (!impl_->open || impl_->items.empty()) {
        return false;
    }

    const auto font = context_menu_font();
    const auto geometry =
        compute_popup_geometry(impl_->position, impl_->items, [&](std::string_view text) {
            return measure_text(text, font);
        });
    return geometry.bounds.contains(point);
}

std::vector<Rect> ContextMenu::damage_regions() const {
    auto regions = Widget::damage_regions();
    if (!impl_->open || impl_->items.empty()) {
        return regions;
    }

    const auto font = context_menu_font();
    const auto geometry =
        compute_popup_geometry(impl_->position, impl_->items, [&](std::string_view text) {
            return measure_text(text, font);
        });
    regions.push_back(geometry.bounds);
    return regions;
}

bool ContextMenu::handle_mouse_event(const MouseEvent& event) {
    if (!impl_->open || impl_->items.empty()) {
        return false;
    }

    const auto font = context_menu_font();
    const auto geometry =
        compute_popup_geometry(impl_->position, impl_->items, [&](std::string_view text) {
            return measure_text(text, font);
        });
    const auto point = Point{event.x, event.y};
    const int index = item_index_at(geometry, impl_->items, point);
    const bool inside_popup = geometry.bounds.contains(point);

    if (event.button != 1 && event.type != MouseEvent::Type::Move &&
        event.type != MouseEvent::Type::Leave) {
        return inside_popup;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        if (!inside_popup) {
            dismiss();
            return false;
        }
        impl_->armed_index = index;
        return true;
    case MouseEvent::Type::Release: {
        if (!inside_popup) {
            dismiss();
            return false;
        }
        const bool activated = impl_->armed_index >= 0 && index == impl_->armed_index;
        const int activated_index = impl_->armed_index;
        impl_->armed_index = -1;
        if (activated) {
            dismiss();
            impl_->item_activated.emit(activated_index);
        }
        return true;
    }
    case MouseEvent::Type::Move:
        if (impl_->hovered_index != index) {
            impl_->hovered_index = index;
            queue_redraw();
        }
        return inside_popup;
    case MouseEvent::Type::Leave:
        if (impl_->hovered_index != -1) {
            impl_->hovered_index = -1;
            queue_redraw();
        }
        return false;
    case MouseEvent::Type::Enter:
        return inside_popup;
    case MouseEvent::Type::Scroll:
        return inside_popup;
    }

    return false;
}

bool ContextMenu::handle_key_event(const KeyEvent& event) {
    if (!impl_->open || event.type != KeyEvent::Type::Press || impl_->items.empty()) {
        return false;
    }

    auto next_selectable = [this](int from, int delta) -> int {
        int index = from;
        for (int i = 0; i < static_cast<int>(impl_->items.size()); ++i) {
            index += delta;
            if (index < 0) {
                index = static_cast<int>(impl_->items.size()) - 1;
            } else if (index >= static_cast<int>(impl_->items.size())) {
                index = 0;
            }
            if (!impl_->items[static_cast<std::size_t>(index)].is_separator) {
                return index;
            }
        }
        return from;
    };

    switch (event.key) {
    case KeyCode::Escape:
        dismiss();
        return true;
    case KeyCode::Up:
        impl_->hovered_index =
            next_selectable(impl_->hovered_index >= 0 ? impl_->hovered_index
                                                      : static_cast<int>(impl_->items.size()),
                            -1);
        queue_redraw();
        return true;
    case KeyCode::Down:
        impl_->hovered_index =
            next_selectable(impl_->hovered_index >= 0 ? impl_->hovered_index : -1, 1);
        queue_redraw();
        return true;
    case KeyCode::Return:
    case KeyCode::Space:
        if (impl_->hovered_index >= 0) {
            const int activated_index = impl_->hovered_index;
            dismiss();
            impl_->item_activated.emit(activated_index);
        }
        return true;
    default:
        return false;
    }
}

void ContextMenu::snapshot(SnapshotContext& ctx) const {
    if (!impl_->open || impl_->items.empty()) {
        return;
    }

    const auto font = context_menu_font();
    const auto geometry =
        compute_popup_geometry(impl_->position, impl_->items, [&](std::string_view text) {
            return measure_text(text, font);
        });

    const float popup_radius = theme_number("popup-radius", 12.0F);
    const auto popup_bg = theme_color("popup-background", Color{1.0F, 1.0F, 1.0F, 1.0F});
    const auto popup_border = theme_color("popup-border-color", Color{0.8F, 0.82F, 0.86F, 1.0F});
    const auto hover_bg = theme_color("hover-background", Color{0.94F, 0.95F, 0.97F, 1.0F});
    const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    const auto separator_color = theme_color("separator-color", Color{0.84F, 0.86F, 0.9F, 1.0F});

    ctx.push_overlay_container(geometry.bounds);

    // Shadow
    ctx.add_rounded_rect({geometry.bounds.x,
                          geometry.bounds.y + 1.0F,
                          geometry.bounds.width,
                          geometry.bounds.height},
                         Color{0.08F, 0.12F, 0.18F, 0.05F},
                         popup_radius + 1.0F);

    // Background
    ctx.add_rounded_rect(geometry.bounds, popup_bg, popup_radius);
    ctx.add_border(geometry.bounds, popup_border, 1.0F, popup_radius);

    // Items
    float y = geometry.bounds.y + 1.0F;
    for (std::size_t i = 0; i < impl_->items.size(); ++i) {
        const auto& item = impl_->items[i];

        if (item.is_separator) {
            const float sep_y = y + geometry.separator_height * 0.5F;
            ctx.add_color_rect({geometry.bounds.x + 8.0F,
                                sep_y,
                                std::max(0.0F, geometry.bounds.width - 16.0F),
                                1.0F},
                               separator_color);
            y += geometry.separator_height;
            continue;
        }

        const auto row_rect = Rect{geometry.bounds.x + 1.0F,
                                   y,
                                   std::max(0.0F, geometry.bounds.width - 2.0F),
                                   geometry.item_height};

        const bool hovered = static_cast<int>(i) == impl_->hovered_index;
        if (hovered) {
            ctx.add_rounded_rect({row_rect.x + 4.0F,
                                  row_rect.y + 2.0F,
                                  row_rect.width - 8.0F,
                                  row_rect.height - 4.0F},
                                 hover_bg,
                                 8.0F);
        }

        const auto measured = measure_text(item.label, font);
        const float text_y = y + std::max(0.0F, (geometry.item_height - measured.height) * 0.5F);
        ctx.add_text({geometry.bounds.x + 12.0F, text_y}, item.label, text_color, font);

        y += geometry.item_height;
    }

    ctx.pop_container();
}

} // namespace nk
