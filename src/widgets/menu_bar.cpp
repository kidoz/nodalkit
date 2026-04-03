#include <algorithm>
#include <functional>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/menu_bar.h>
#include <optional>

namespace nk {

namespace {

FontDescriptor menu_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

Size approximate_text_size(std::string_view text, const FontDescriptor& font) {
    if (text.empty()) {
        return {};
    }

    const auto length = static_cast<float>(text.size());
    return {length * font.size * 0.55F, font.size * 1.35F};
}

struct PopupGeometry {
    Rect bounds;
    float item_height = 28.0F;
};

using MenuPath = std::vector<int>;

void invalidate_popup_frame(Widget& widget) {
    widget.queue_layout();
    widget.queue_redraw();
}

Rect popup_item_rect(const PopupGeometry& geometry, int index) {
    return {
        geometry.bounds.x + 1.0F,
        geometry.bounds.y + 1.0F + (static_cast<float>(index) * geometry.item_height),
        std::max(0.0F, geometry.bounds.width - 2.0F),
        geometry.item_height,
    };
}

} // namespace

// --- MenuItem factory helpers ---

MenuItem MenuItem::action(std::string label, std::string action) {
    MenuItem item;
    item.label = std::move(label);
    item.action_name = std::move(action);
    return item;
}

MenuItem MenuItem::submenu(std::string label, std::vector<MenuItem> items) {
    MenuItem item;
    item.label = std::move(label);
    item.children = std::move(items);
    return item;
}

MenuItem MenuItem::make_separator() {
    MenuItem item;
    item.separator = true;
    return item;
}

// --- MenuBar ---

struct MenuBar::Impl {
    std::vector<Menu> menus;
    Signal<std::string_view> action_signal;
    int open_menu = -1;
    int armed_menu = -1;
    MenuPath open_path;
    MenuPath highlighted_path;
    MenuPath armed_path;
};

std::shared_ptr<MenuBar> MenuBar::create() {
    return std::shared_ptr<MenuBar>(new MenuBar());
}

MenuBar::MenuBar() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("menu-bar");
    ensure_accessible().set_role(AccessibleRole::MenuBar);
}

MenuBar::~MenuBar() = default;

void MenuBar::add_menu(Menu menu) {
    impl_->menus.push_back(std::move(menu));
    queue_layout();
    queue_redraw();
}

void MenuBar::clear() {
    impl_->menus.clear();
    queue_layout();
    queue_redraw();
}

Signal<std::string_view>& MenuBar::on_action() {
    return impl_->action_signal;
}

namespace {

std::vector<Rect> menu_title_rects(const MenuBar& bar, const std::vector<Menu>& menus) {
    std::vector<Rect> rects;
    rects.reserve(menus.size());

    const auto a = bar.allocation();
    float x_offset = 18.0F;
    const auto font = menu_font();
    for (const auto& menu : menus) {
        const auto measured = approximate_text_size(menu.title, font);
        const float width = measured.width + 22.0F;
        rects.push_back({a.x + x_offset - 8.0F, a.y, width, a.height});
        x_offset += measured.width + 22.0F;
    }

    return rects;
}

int menu_at_point(const MenuBar& bar, const std::vector<Menu>& menus, Point point) {
    const auto rects = menu_title_rects(bar, menus);
    for (std::size_t index = 0; index < rects.size(); ++index) {
        if (rects[index].contains(point)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

float popup_width(const MenuBar& bar, const std::vector<MenuItem>& items) {
    const auto font = menu_font();
    float width = 160.0F;
    (void)bar;
    for (const auto& item : items) {
        if (item.separator) {
            continue;
        }
        const auto measured = approximate_text_size(item.label, font);
        width = std::max(width, measured.width + (item.children.empty() ? 28.0F : 44.0F));
    }
    return width;
}

PopupGeometry
popup_geometry(const MenuBar& bar, Rect anchor, const std::vector<MenuItem>& items, bool submenu) {
    PopupGeometry geometry;
    (void)bar;
    geometry.item_height = 28.0F;
    geometry.bounds = submenu
                          ? Rect{anchor.right() + 4.0F,
                                 anchor.y - 4.0F,
                                 popup_width(bar, items),
                                 static_cast<float>(items.size()) * geometry.item_height + 2.0F}
                          : Rect{anchor.x,
                                 anchor.bottom() + 4.0F,
                                 popup_width(bar, items),
                                 static_cast<float>(items.size()) * geometry.item_height + 2.0F};
    return geometry;
}

int popup_index_at(const PopupGeometry& geometry, const std::vector<MenuItem>& items, Point point) {
    if (!geometry.bounds.contains(point)) {
        return -1;
    }
    const int index = static_cast<int>((point.y - geometry.bounds.y - 1.0F) / geometry.item_height);
    if (index < 0 || index >= static_cast<int>(items.size())) {
        return -1;
    }
    return index;
}

const MenuItem* item_at_path(const Menu& menu, const MenuPath& path) {
    const std::vector<MenuItem>* items = &menu.items;
    const MenuItem* current = nullptr;
    for (int index : path) {
        if (index < 0 || index >= static_cast<int>(items->size())) {
            return nullptr;
        }
        current = &(*items)[static_cast<std::size_t>(index)];
        items = &current->children;
    }
    return current;
}

const std::vector<MenuItem>* items_for_prefix(const Menu& menu, const MenuPath& prefix) {
    const std::vector<MenuItem>* items = &menu.items;
    for (int index : prefix) {
        if (index < 0 || index >= static_cast<int>(items->size())) {
            return nullptr;
        }
        items = &(*items)[static_cast<std::size_t>(index)].children;
    }
    return items;
}

MenuPath submenu_chain_for_path(const Menu& menu, const MenuPath& path) {
    MenuPath chain;
    const std::vector<MenuItem>* items = &menu.items;
    for (int index : path) {
        if (index < 0 || index >= static_cast<int>(items->size())) {
            break;
        }
        const auto& item = (*items)[static_cast<std::size_t>(index)];
        if (item.children.empty()) {
            break;
        }
        chain.push_back(index);
        items = &item.children;
    }
    return chain;
}

std::optional<MenuPath> popup_path_at(const MenuBar& bar,
                                      const std::vector<Menu>& menus,
                                      const Menu& menu,
                                      int menu_index,
                                      const MenuPath& open_path,
                                      Point point) {
    const auto all_title_rects = menu_title_rects(bar, menus);
    if (menu_index < 0 || menu_index >= static_cast<int>(all_title_rects.size())) {
        return std::nullopt;
    }

    std::function<std::optional<MenuPath>(
        const std::vector<MenuItem>&, Rect, bool, std::size_t, MenuPath)>
        recurse;
    recurse = [&](const std::vector<MenuItem>& items,
                  Rect anchor,
                  bool submenu,
                  std::size_t depth,
                  MenuPath prefix) -> std::optional<MenuPath> {
        const auto geometry = popup_geometry(bar, anchor, items, submenu);
        const int index = popup_index_at(geometry, items, point);
        if (index >= 0) {
            auto path = prefix;
            path.push_back(index);
            if (depth < open_path.size() && open_path[depth] == index &&
                !items[static_cast<std::size_t>(index)].children.empty()) {
                if (auto nested = recurse(items[static_cast<std::size_t>(index)].children,
                                          popup_item_rect(geometry, index),
                                          true,
                                          depth + 1,
                                          path)) {
                    return nested;
                }
            }
            return path;
        }

        if (depth < open_path.size()) {
            const int open_index = open_path[depth];
            if (open_index >= 0 && open_index < static_cast<int>(items.size()) &&
                !items[static_cast<std::size_t>(open_index)].children.empty()) {
                auto nested_prefix = prefix;
                nested_prefix.push_back(open_index);
                return recurse(items[static_cast<std::size_t>(open_index)].children,
                               popup_item_rect(geometry, open_index),
                               true,
                               depth + 1,
                               nested_prefix);
            }
        }

        return std::nullopt;
    };

    return recurse(menu.items, all_title_rects[static_cast<std::size_t>(menu_index)], false, 0, {});
}

bool popup_contains_point(const MenuBar& bar,
                          const std::vector<Menu>& menus,
                          const Menu& menu,
                          int menu_index,
                          const MenuPath& open_path,
                          Point point) {
    const auto title_rects = menu_title_rects(bar, menus);
    if (menu_index < 0 || menu_index >= static_cast<int>(title_rects.size())) {
        return false;
    }

    std::function<bool(const std::vector<MenuItem>&, Rect, bool, std::size_t)> recurse;
    recurse = [&](const std::vector<MenuItem>& items,
                  Rect anchor,
                  bool submenu,
                  std::size_t depth) -> bool {
        const auto geometry = popup_geometry(bar, anchor, items, submenu);
        if (geometry.bounds.contains(point)) {
            return true;
        }

        if (depth < open_path.size()) {
            const int open_index = open_path[depth];
            if (open_index >= 0 && open_index < static_cast<int>(items.size()) &&
                !items[static_cast<std::size_t>(open_index)].children.empty()) {
                return recurse(items[static_cast<std::size_t>(open_index)].children,
                               popup_item_rect(geometry, open_index),
                               true,
                               depth + 1);
            }
        }
        return false;
    };

    return recurse(menu.items, title_rects[static_cast<std::size_t>(menu_index)], false, 0);
}

} // namespace

SizeRequest MenuBar::measure(const Constraints& constraints) const {
    const float h = theme_number("min-height", 32.0F);
    const float w = constraints.max_width;
    return {0, h, w, h};
}

bool MenuBar::handle_mouse_event(const MouseEvent& event) {
    const auto point = Point{event.x, event.y};
    const int hovered_menu = menu_at_point(*this, impl_->menus, point);
    const bool popup_hit =
        impl_->open_menu >= 0 &&
        popup_contains_point(*this,
                             impl_->menus,
                             impl_->menus[static_cast<std::size_t>(impl_->open_menu)],
                             impl_->open_menu,
                             impl_->open_path,
                             point);

    auto close_popups = [this] {
        impl_->open_menu = -1;
        impl_->armed_menu = -1;
        impl_->open_path.clear();
        impl_->highlighted_path.clear();
        impl_->armed_path.clear();
        invalidate_popup_frame(*this);
    };

    if (event.button != 1 && event.type != MouseEvent::Type::Move &&
        event.type != MouseEvent::Type::Leave) {
        return hovered_menu >= 0 || popup_hit;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->armed_menu = hovered_menu;
        impl_->armed_path.clear();
        if (impl_->open_menu >= 0 && popup_hit) {
            if (auto path = popup_path_at(*this,
                                          impl_->menus,
                                          impl_->menus[static_cast<std::size_t>(impl_->open_menu)],
                                          impl_->open_menu,
                                          impl_->open_path,
                                          point)) {
                impl_->armed_path = *path;
            }
        }
        return hovered_menu >= 0 || popup_hit;
    case MouseEvent::Type::Release:
        if (hovered_menu >= 0 && impl_->armed_menu == hovered_menu && impl_->armed_path.empty()) {
            if (impl_->open_menu == hovered_menu) {
                close_popups();
            } else {
                impl_->open_menu = hovered_menu;
                impl_->open_path.clear();
                impl_->highlighted_path.clear();
                impl_->armed_menu = -1;
                invalidate_popup_frame(*this);
            }
            return true;
        }

        if (impl_->open_menu >= 0 && !impl_->armed_path.empty()) {
            auto released_path =
                popup_path_at(*this,
                              impl_->menus,
                              impl_->menus[static_cast<std::size_t>(impl_->open_menu)],
                              impl_->open_menu,
                              impl_->open_path,
                              point);
            if (released_path && *released_path == impl_->armed_path) {
                const auto& menu = impl_->menus[static_cast<std::size_t>(impl_->open_menu)];
                if (auto* item = item_at_path(menu, *released_path)) {
                    if (!item->enabled || item->separator) {
                        return true;
                    }
                    if (!item->children.empty()) {
                        impl_->open_path = *released_path;
                        impl_->highlighted_path = *released_path;
                        invalidate_popup_frame(*this);
                        return true;
                    }
                    if (!item->action_name.empty()) {
                        impl_->action_signal.emit(item->action_name);
                    }
                    close_popups();
                    return true;
                }
            }
        }

        if (impl_->open_menu >= 0 && !popup_hit && hovered_menu < 0) {
            close_popups();
        }
        impl_->armed_menu = -1;
        impl_->armed_path.clear();
        return hovered_menu >= 0 || popup_hit;
    case MouseEvent::Type::Move:
        if (impl_->open_menu >= 0) {
            if (hovered_menu >= 0 && hovered_menu != impl_->open_menu) {
                impl_->open_menu = hovered_menu;
                impl_->open_path.clear();
                impl_->highlighted_path.clear();
                invalidate_popup_frame(*this);
                return true;
            }

            if (popup_hit) {
                if (auto path =
                        popup_path_at(*this,
                                      impl_->menus,
                                      impl_->menus[static_cast<std::size_t>(impl_->open_menu)],
                                      impl_->open_menu,
                                      impl_->open_path,
                                      point)) {
                    const auto next_open_path = submenu_chain_for_path(
                        impl_->menus[static_cast<std::size_t>(impl_->open_menu)], *path);
                    if (impl_->highlighted_path != *path || impl_->open_path != next_open_path) {
                        impl_->highlighted_path = *path;
                        impl_->open_path = next_open_path;
                        invalidate_popup_frame(*this);
                    }
                    return true;
                }
            }
        }
        return hovered_menu >= 0 || popup_hit;
    case MouseEvent::Type::Leave:
        return false;
    case MouseEvent::Type::Enter:
        return hovered_menu >= 0 || popup_hit;
    case MouseEvent::Type::Scroll:
        return popup_hit;
    }

    return false;
}

bool MenuBar::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press || impl_->menus.empty()) {
        return false;
    }

    auto close_popups = [this] {
        impl_->open_menu = -1;
        impl_->open_path.clear();
        impl_->highlighted_path.clear();
        invalidate_popup_frame(*this);
    };

    auto select_next_menu = [this](int delta) {
        if (impl_->menus.empty()) {
            return;
        }
        if (impl_->open_menu < 0) {
            impl_->open_menu = delta >= 0 ? 0 : static_cast<int>(impl_->menus.size()) - 1;
        } else {
            impl_->open_menu = (impl_->open_menu + delta + static_cast<int>(impl_->menus.size())) %
                               static_cast<int>(impl_->menus.size());
        }
        impl_->open_path.clear();
        impl_->highlighted_path.clear();
        invalidate_popup_frame(*this);
    };

    if (impl_->open_menu < 0) {
        switch (event.key) {
        case KeyCode::Left:
            select_next_menu(-1);
            return true;
        case KeyCode::Right:
        case KeyCode::Down:
        case KeyCode::Return:
        case KeyCode::Space:
            select_next_menu(1);
            return true;
        default:
            return false;
        }
    }

    const auto& menu = impl_->menus[static_cast<std::size_t>(impl_->open_menu)];
    auto current_path = impl_->highlighted_path;
    const std::vector<MenuItem>* current_items = &menu.items;
    if (!impl_->open_path.empty()) {
        current_items = items_for_prefix(menu, impl_->open_path);
        current_path = impl_->open_path;
    }
    if (current_items == nullptr || current_items->empty()) {
        current_items = &menu.items;
        current_path.clear();
    }

    auto move_highlight = [&](int delta) {
        int current_index = current_path.empty() ? -1 : current_path.back();
        int next_index = current_index;
        do {
            next_index =
                std::clamp(next_index + delta, 0, static_cast<int>(current_items->size()) - 1);
            const auto& item = (*current_items)[static_cast<std::size_t>(next_index)];
            if (!item.separator && item.enabled) {
                auto next_path = current_path;
                if (next_path.empty()) {
                    next_path.push_back(next_index);
                } else {
                    next_path.back() = next_index;
                }
                impl_->highlighted_path = next_path;
                impl_->open_path = submenu_chain_for_path(menu, next_path);
                invalidate_popup_frame(*this);
                break;
            }
            if (next_index == current_index) {
                break;
            }
        } while (next_index > 0 && next_index + 1 < static_cast<int>(current_items->size()));
    };

    switch (event.key) {
    case KeyCode::Escape:
        close_popups();
        return true;
    case KeyCode::Left:
        if (!impl_->open_path.empty()) {
            impl_->open_path.pop_back();
            impl_->highlighted_path = impl_->open_path;
            invalidate_popup_frame(*this);
            return true;
        }
        select_next_menu(-1);
        return true;
    case KeyCode::Right:
        if (!impl_->highlighted_path.empty()) {
            if (auto* item = item_at_path(menu, impl_->highlighted_path);
                item != nullptr && !item->children.empty()) {
                impl_->open_path = impl_->highlighted_path;
                invalidate_popup_frame(*this);
                return true;
            }
        }
        select_next_menu(1);
        return true;
    case KeyCode::Up:
        move_highlight(-1);
        return true;
    case KeyCode::Down:
        move_highlight(1);
        return true;
    case KeyCode::Return:
    case KeyCode::Space:
        if (impl_->highlighted_path.empty()) {
            move_highlight(1);
            return true;
        }
        if (auto* item = item_at_path(menu, impl_->highlighted_path)) {
            if (!item->children.empty()) {
                impl_->open_path = impl_->highlighted_path;
                invalidate_popup_frame(*this);
                return true;
            }
            if (item->enabled && !item->separator && !item->action_name.empty()) {
                impl_->action_signal.emit(item->action_name);
            }
            close_popups();
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool MenuBar::hit_test(Point point) const {
    if (Widget::hit_test(point)) {
        return true;
    }

    if (impl_->open_menu < 0) {
        return false;
    }

    return popup_contains_point(*this,
                                impl_->menus,
                                impl_->menus[static_cast<std::size_t>(impl_->open_menu)],
                                impl_->open_menu,
                                impl_->open_path,
                                point);
}

CursorShape MenuBar::cursor_shape() const {
    return CursorShape::PointingHand;
}

void MenuBar::on_focus_changed(bool focused) {
    if (focused || impl_->open_menu < 0) {
        return;
    }

    impl_->open_menu = -1;
    impl_->armed_menu = -1;
    impl_->open_path.clear();
    impl_->highlighted_path.clear();
    impl_->armed_path.clear();
    invalidate_popup_frame(*this);
}

void MenuBar::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    ctx.add_color_rect(a, theme_color("background", Color{0.95F, 0.96F, 0.98F, 1.0F}));
    ctx.add_color_rect({a.x, a.y + a.height - 1, a.width, 1},
                       theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F}));

    const auto text_color = theme_color("text-color", Color{0.15F, 0.15F, 0.15F, 1.0F});
    const auto font = menu_font();
    const auto title_rects = menu_title_rects(*this, impl_->menus);
    for (std::size_t index = 0; index < impl_->menus.size(); ++index) {
        const auto& menu = impl_->menus[index];
        const auto measured = measure_text(menu.title, font);
        const auto title_rect = title_rects[index];
        if (static_cast<int>(index) == impl_->open_menu) {
            ctx.add_rounded_rect(
                {title_rect.x + 2.0F,
                 title_rect.y + 4.0F,
                 title_rect.width - 4.0F,
                 title_rect.height - 8.0F},
                theme_color("menu-hover-background", Color{0.94F, 0.95F, 0.97F, 1.0F}),
                8.0F);
        }

        const float text_y = a.y + std::max(0.0F, (a.height - measured.height) * 0.5F);
        ctx.add_text({title_rect.x + 8.0F, text_y}, menu.title, text_color, font);
    }

    if (impl_->open_menu < 0 || impl_->open_menu >= static_cast<int>(impl_->menus.size())) {
        return;
    }

    const auto popup_bg = theme_color("popup-background", Color{1.0F, 1.0F, 1.0F, 1.0F});
    const auto popup_border = theme_color("popup-border-color", Color{0.8F, 0.82F, 0.86F, 1.0F});
    const auto popup_hover =
        theme_color("popup-hover-background", Color{0.94F, 0.95F, 0.97F, 1.0F});
    const auto popup_text = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    const auto disabled_text = theme_color("disabled-text-color", Color{0.55F, 0.58F, 0.62F, 1.0F});

    const auto& menu = impl_->menus[static_cast<std::size_t>(impl_->open_menu)];
    std::function<void(const std::vector<MenuItem>&, Rect, bool, std::size_t, MenuPath)>
        paint_popup;
    paint_popup = [&](const std::vector<MenuItem>& items,
                      Rect anchor,
                      bool submenu,
                      std::size_t depth,
                      MenuPath prefix) {
        if (items.empty()) {
            return;
        }

        const auto geometry = popup_geometry(*this, anchor, items, submenu);
        ctx.push_overlay_container(geometry.bounds);
        ctx.add_rounded_rect({geometry.bounds.x,
                              geometry.bounds.y + 1.0F,
                              geometry.bounds.width,
                              geometry.bounds.height},
                             Color{0.08F, 0.12F, 0.18F, 0.05F},
                             12.0F);
        ctx.add_rounded_rect(geometry.bounds, popup_bg, 12.0F);
        ctx.add_border(geometry.bounds, popup_border, 1.0F, 12.0F);

        for (std::size_t index = 0; index < items.size(); ++index) {
            const auto& item = items[index];
            auto row_rect = popup_item_rect(geometry, static_cast<int>(index));

            if (item.separator) {
                ctx.add_color_rect({row_rect.x + 8.0F,
                                    row_rect.y + (row_rect.height * 0.5F),
                                    std::max(0.0F, row_rect.width - 16.0F),
                                    1.0F},
                                   popup_border);
                continue;
            }

            MenuPath row_path = prefix;
            row_path.push_back(static_cast<int>(index));
            const bool highlighted =
                impl_->highlighted_path == row_path || impl_->open_path == row_path;
            if (highlighted) {
                ctx.add_rounded_rect({row_rect.x + 4.0F,
                                      row_rect.y + 2.0F,
                                      row_rect.width - 8.0F,
                                      row_rect.height - 4.0F},
                                     popup_hover,
                                     8.0F);
            }

            const auto label_size = measure_text(item.label, font);
            const float text_y =
                row_rect.y + std::max(0.0F, (row_rect.height - label_size.height) * 0.5F);
            ctx.add_text({row_rect.x + 12.0F, text_y},
                         item.label,
                         item.enabled ? popup_text : disabled_text,
                         font);

            if (!item.children.empty()) {
                const auto arrow_size = measure_text(">", font);
                ctx.add_text(
                    {row_rect.right() - arrow_size.width - 12.0F, text_y}, ">", popup_text, font);
            }
        }
        ctx.pop_container();

        if (depth < impl_->open_path.size()) {
            const int open_index = impl_->open_path[depth];
            if (open_index >= 0 && open_index < static_cast<int>(items.size()) &&
                !items[static_cast<std::size_t>(open_index)].children.empty()) {
                auto next_prefix = prefix;
                next_prefix.push_back(open_index);
                paint_popup(items[static_cast<std::size_t>(open_index)].children,
                            popup_item_rect(geometry, open_index),
                            true,
                            depth + 1,
                            next_prefix);
            }
        }
    };

    paint_popup(menu.items, title_rects[static_cast<std::size_t>(impl_->open_menu)], false, 0, {});
}

} // namespace nk
