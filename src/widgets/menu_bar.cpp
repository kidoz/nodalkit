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

struct PopupGeometry {
    Rect bounds;
    float item_height = 28.0F;
};

using MenuPath = std::vector<int>;

Rect union_rect(Rect lhs, Rect rhs) {
    if (lhs.width <= 0.0F || lhs.height <= 0.0F) {
        return rhs;
    }
    if (rhs.width <= 0.0F || rhs.height <= 0.0F) {
        return lhs;
    }
    const float left = std::min(lhs.x, rhs.x);
    const float top = std::min(lhs.y, rhs.y);
    const float right = std::max(lhs.right(), rhs.right());
    const float bottom = std::max(lhs.bottom(), rhs.bottom());
    return {left, top, right - left, bottom - top};
}

Rect to_local_rect(Rect rect, Rect allocation) {
    return {rect.x - allocation.x, rect.y - allocation.y, rect.width, rect.height};
}

Rect title_damage_rect(const std::vector<Rect>& rects, Rect allocation, int menu_index) {
    if (menu_index < 0 || menu_index >= static_cast<int>(rects.size())) {
        return {};
    }
    return to_local_rect(rects[static_cast<std::size_t>(menu_index)], allocation);
}

Rect title_damage_rect(const std::vector<Rect>& rects,
                       Rect allocation,
                       int first_menu,
                       int second_menu) {
    return union_rect(title_damage_rect(rects, allocation, first_menu),
                      title_damage_rect(rects, allocation, second_menu));
}

void invalidate_popup_frame(Widget& widget, Rect local_damage) {
    if (local_damage.width <= 0.0F || local_damage.height <= 0.0F) {
        widget.queue_redraw();
        return;
    }
    widget.queue_redraw(local_damage);
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

template <typename MeasureTextFn>
std::vector<Rect> menu_title_rects(Rect bar_rect,
                                   const std::vector<Menu>& menus,
                                   MeasureTextFn&& measure_text_fn) {
    std::vector<Rect> rects;
    rects.reserve(menus.size());

    float x_offset = 18.0F;
    for (const auto& menu : menus) {
        const auto measured = measure_text_fn(menu.title);
        const float width = measured.width + 22.0F;
        rects.push_back({bar_rect.x + x_offset - 8.0F, bar_rect.y, width, bar_rect.height});
        x_offset += measured.width + 22.0F;
    }

    return rects;
}

int menu_at_point(const std::vector<Rect>& rects, Point point) {
    for (std::size_t index = 0; index < rects.size(); ++index) {
        if (rects[index].contains(point)) {
            return static_cast<int>(index);
        }
    }
    return -1;
}

template <typename MeasureTextFn>
float popup_width(const std::vector<MenuItem>& items, MeasureTextFn&& measure_text_fn) {
    float width = 160.0F;
    for (const auto& item : items) {
        if (item.separator) {
            continue;
        }
        const auto measured = measure_text_fn(item.label);
        width = std::max(width, measured.width + (item.children.empty() ? 28.0F : 44.0F));
    }
    return width;
}

template <typename MeasureTextFn>
PopupGeometry
popup_geometry(Rect anchor,
               const std::vector<MenuItem>& items,
               bool submenu,
               MeasureTextFn&& measure_text_fn) {
    PopupGeometry geometry;
    geometry.item_height = 28.0F;
    geometry.bounds = submenu
                          ? Rect{anchor.right() + 4.0F,
                                 anchor.y - 4.0F,
                                 popup_width(items, measure_text_fn),
                                 static_cast<float>(items.size()) * geometry.item_height + 2.0F}
                          : Rect{anchor.x,
                                 anchor.bottom() + 4.0F,
                                 popup_width(items, measure_text_fn),
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

template <typename MeasureTextFn>
std::optional<MenuPath> popup_path_at(const std::vector<Rect>& title_rects,
                                      const Menu& menu,
                                      int menu_index,
                                      const MenuPath& open_path,
                                      Point point,
                                      MeasureTextFn&& measure_text_fn) {
    if (menu_index < 0 || menu_index >= static_cast<int>(title_rects.size())) {
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
        const auto geometry = popup_geometry(anchor, items, submenu, measure_text_fn);
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

    return recurse(menu.items, title_rects[static_cast<std::size_t>(menu_index)], false, 0, {});
}

template <typename MeasureTextFn>
bool popup_contains_point(const std::vector<Rect>& title_rects,
                          const Menu& menu,
                          int menu_index,
                          const MenuPath& open_path,
                          Point point,
                          MeasureTextFn&& measure_text_fn) {
    if (menu_index < 0 || menu_index >= static_cast<int>(title_rects.size())) {
        return false;
    }

    std::function<bool(const std::vector<MenuItem>&, Rect, bool, std::size_t)> recurse;
    recurse = [&](const std::vector<MenuItem>& items,
                  Rect anchor,
                  bool submenu,
                  std::size_t depth) -> bool {
        const auto geometry = popup_geometry(anchor, items, submenu, measure_text_fn);
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

template <typename MeasureTextFn>
std::vector<Rect> popup_damage_regions(Rect /*bar_rect*/,
                                       const std::vector<Rect>& title_rects,
                                       const Menu& menu,
                                       int menu_index,
                                       const MenuPath& open_path,
                                       MeasureTextFn&& measure_text_fn) {
    std::vector<Rect> regions;
    if (menu_index < 0 || menu_index >= static_cast<int>(title_rects.size())) {
        return regions;
    }

    std::function<void(const std::vector<MenuItem>&, Rect, bool, std::size_t)> recurse;
    recurse =
        [&](const std::vector<MenuItem>& items, Rect anchor, bool submenu, std::size_t depth) {
            const auto geometry = popup_geometry(anchor, items, submenu, measure_text_fn);
            regions.push_back(geometry.bounds);
            if (depth >= open_path.size()) {
                return;
            }

            const int open_index = open_path[depth];
            if (open_index < 0 || open_index >= static_cast<int>(items.size())) {
                return;
            }

            const auto& item = items[static_cast<std::size_t>(open_index)];
            if (item.children.empty()) {
                return;
            }

            recurse(item.children, popup_item_rect(geometry, open_index), true, depth + 1);
        };

    recurse(menu.items, title_rects[static_cast<std::size_t>(menu_index)], false, 0);
    return regions;
}

} // namespace

SizeRequest MenuBar::measure(const Constraints& constraints) const {
    const float h = theme_number("min-height", 32.0F);
    const float w = constraints.max_width;
    return {0, h, w, h};
}

bool MenuBar::handle_mouse_event(const MouseEvent& event) {
    const auto point = Point{event.x, event.y};
    const auto font = menu_font();
    const auto measure_menu_text = [&](std::string_view text) { return measure_text(text, font); };
    const auto title_rects = menu_title_rects(allocation(), impl_->menus, measure_menu_text);
    const int hovered_menu = menu_at_point(title_rects, point);
    const bool popup_hit =
        impl_->open_menu >= 0 &&
        popup_contains_point(title_rects,
                             impl_->menus[static_cast<std::size_t>(impl_->open_menu)],
                             impl_->open_menu,
                             impl_->open_path,
                             point,
                             measure_menu_text);

    auto close_popups = [this, &title_rects] {
        const int previous_open_menu = impl_->open_menu;
        preserve_damage_regions_for_next_redraw();
        impl_->open_menu = -1;
        impl_->armed_menu = -1;
        impl_->open_path.clear();
        impl_->highlighted_path.clear();
        impl_->armed_path.clear();
        invalidate_popup_frame(*this,
                               title_damage_rect(title_rects, allocation(), previous_open_menu));
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
            if (auto path = popup_path_at(title_rects,
                                          impl_->menus[static_cast<std::size_t>(impl_->open_menu)],
                                          impl_->open_menu,
                                          impl_->open_path,
                                          point,
                                          measure_menu_text)) {
                impl_->armed_path = *path;
            }
        }
        return hovered_menu >= 0 || popup_hit;
    case MouseEvent::Type::Release:
        if (hovered_menu >= 0 && impl_->armed_menu == hovered_menu && impl_->armed_path.empty()) {
            if (impl_->open_menu == hovered_menu) {
                close_popups();
            } else {
                const int previous_open_menu = impl_->open_menu;
                preserve_damage_regions_for_next_redraw();
                impl_->open_menu = hovered_menu;
                impl_->open_path.clear();
                impl_->highlighted_path.clear();
                impl_->armed_menu = -1;
                invalidate_popup_frame(
                    *this,
                    title_damage_rect(title_rects, allocation(), previous_open_menu, hovered_menu));
            }
            return true;
        }

        if (impl_->open_menu >= 0 && !impl_->armed_path.empty()) {
            auto released_path =
                popup_path_at(title_rects,
                              impl_->menus[static_cast<std::size_t>(impl_->open_menu)],
                              impl_->open_menu,
                              impl_->open_path,
                              point,
                              measure_menu_text);
            if (released_path && *released_path == impl_->armed_path) {
                const auto& menu = impl_->menus[static_cast<std::size_t>(impl_->open_menu)];
                if (auto* item = item_at_path(menu, *released_path)) {
                    if (!item->enabled || item->separator) {
                        return true;
                    }
                    if (!item->children.empty()) {
                        preserve_damage_regions_for_next_redraw();
                        impl_->open_path = *released_path;
                        impl_->highlighted_path = *released_path;
                        invalidate_popup_frame(
                            *this,
                            title_damage_rect(title_rects, allocation(), impl_->open_menu));
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
                const int previous_open_menu = impl_->open_menu;
                preserve_damage_regions_for_next_redraw();
                impl_->open_menu = hovered_menu;
                impl_->open_path.clear();
                impl_->highlighted_path.clear();
                invalidate_popup_frame(
                    *this,
                    title_damage_rect(title_rects, allocation(), previous_open_menu, hovered_menu));
                return true;
            }

            if (popup_hit) {
                if (auto path =
                        popup_path_at(title_rects,
                                      impl_->menus[static_cast<std::size_t>(impl_->open_menu)],
                                      impl_->open_menu,
                                      impl_->open_path,
                                      point,
                                      measure_menu_text)) {
                    const auto next_open_path = submenu_chain_for_path(
                        impl_->menus[static_cast<std::size_t>(impl_->open_menu)], *path);
                    if (impl_->highlighted_path != *path || impl_->open_path != next_open_path) {
                        if (impl_->open_path != next_open_path) {
                            preserve_damage_regions_for_next_redraw();
                        }
                        impl_->highlighted_path = *path;
                        impl_->open_path = next_open_path;
                        invalidate_popup_frame(
                            *this,
                            title_damage_rect(title_rects, allocation(), impl_->open_menu));
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
        const auto font = menu_font();
        const auto measure_menu_text = [&](std::string_view text) { return measure_text(text, font); };
        const auto title_rects = menu_title_rects(allocation(), impl_->menus, measure_menu_text);
        const int previous_open_menu = impl_->open_menu;
        preserve_damage_regions_for_next_redraw();
        impl_->open_menu = -1;
        impl_->open_path.clear();
        impl_->highlighted_path.clear();
        invalidate_popup_frame(*this,
                               title_damage_rect(title_rects, allocation(), previous_open_menu));
    };

    auto select_next_menu = [this](int delta) {
        if (impl_->menus.empty()) {
            return;
        }
        const auto font = menu_font();
        const auto measure_menu_text = [&](std::string_view text) { return measure_text(text, font); };
        const auto title_rects = menu_title_rects(allocation(), impl_->menus, measure_menu_text);
        const int previous_open_menu = impl_->open_menu;
        preserve_damage_regions_for_next_redraw();
        if (impl_->open_menu < 0) {
            impl_->open_menu = delta >= 0 ? 0 : static_cast<int>(impl_->menus.size()) - 1;
        } else {
            impl_->open_menu = (impl_->open_menu + delta + static_cast<int>(impl_->menus.size())) %
                               static_cast<int>(impl_->menus.size());
        }
        impl_->open_path.clear();
        impl_->highlighted_path.clear();
        invalidate_popup_frame(
            *this,
            title_damage_rect(title_rects, allocation(), previous_open_menu, impl_->open_menu));
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
                preserve_damage_regions_for_next_redraw();
                impl_->open_path = submenu_chain_for_path(menu, next_path);
                const auto font = menu_font();
                const auto measure_menu_text = [&](std::string_view text) {
                    return measure_text(text, font);
                };
                const auto title_rects =
                    menu_title_rects(allocation(), impl_->menus, measure_menu_text);
                invalidate_popup_frame(
                    *this,
                    title_damage_rect(title_rects, allocation(), impl_->open_menu));
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
            preserve_damage_regions_for_next_redraw();
            impl_->open_path.pop_back();
            impl_->highlighted_path = impl_->open_path;
            const auto font = menu_font();
            const auto measure_menu_text = [&](std::string_view text) { return measure_text(text, font); };
            const auto title_rects = menu_title_rects(allocation(), impl_->menus, measure_menu_text);
            invalidate_popup_frame(*this,
                                   title_damage_rect(title_rects, allocation(), impl_->open_menu));
            return true;
        }
        select_next_menu(-1);
        return true;
    case KeyCode::Right:
        if (!impl_->highlighted_path.empty()) {
            if (auto* item = item_at_path(menu, impl_->highlighted_path);
                item != nullptr && !item->children.empty()) {
                preserve_damage_regions_for_next_redraw();
                impl_->open_path = impl_->highlighted_path;
                const auto font = menu_font();
                const auto measure_menu_text = [&](std::string_view text) {
                    return measure_text(text, font);
                };
                const auto title_rects =
                    menu_title_rects(allocation(), impl_->menus, measure_menu_text);
                invalidate_popup_frame(
                    *this,
                    title_damage_rect(title_rects, allocation(), impl_->open_menu));
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
                preserve_damage_regions_for_next_redraw();
                impl_->open_path = impl_->highlighted_path;
                const auto font = menu_font();
                const auto measure_menu_text = [&](std::string_view text) {
                    return measure_text(text, font);
                };
                const auto title_rects =
                    menu_title_rects(allocation(), impl_->menus, measure_menu_text);
                invalidate_popup_frame(
                    *this,
                    title_damage_rect(title_rects, allocation(), impl_->open_menu));
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

    const auto font = menu_font();
    const auto measure_menu_text = [&](std::string_view text) { return measure_text(text, font); };
    const auto title_rects = menu_title_rects(allocation(), impl_->menus, measure_menu_text);
    return popup_contains_point(title_rects,
                                impl_->menus[static_cast<std::size_t>(impl_->open_menu)],
                                impl_->open_menu,
                                impl_->open_path,
                                point,
                                measure_menu_text);
}

std::vector<Rect> MenuBar::damage_regions() const {
    auto regions = Widget::damage_regions();
    if (impl_->open_menu < 0) {
        return regions;
    }
    const auto font = menu_font();
    const auto measure_menu_text = [&](std::string_view text) { return measure_text(text, font); };
    const auto title_rects = menu_title_rects(allocation(), impl_->menus, measure_menu_text);
    auto popup_regions = popup_damage_regions(allocation(),
                                              title_rects,
                                              impl_->menus[static_cast<std::size_t>(impl_->open_menu)],
                                              impl_->open_menu,
                                              impl_->open_path,
                                              measure_menu_text);
    regions.insert(regions.end(), popup_regions.begin(), popup_regions.end());
    return regions;
}

CursorShape MenuBar::cursor_shape() const {
    return CursorShape::PointingHand;
}

void MenuBar::on_focus_changed(bool focused) {
    if (focused || impl_->open_menu < 0) {
        return;
    }

    const auto font = menu_font();
    const auto measure_menu_text = [&](std::string_view text) { return measure_text(text, font); };
    const auto title_rects = menu_title_rects(allocation(), impl_->menus, measure_menu_text);
    const int previous_open_menu = impl_->open_menu;
    preserve_damage_regions_for_next_redraw();
    impl_->open_menu = -1;
    impl_->armed_menu = -1;
    impl_->open_path.clear();
    impl_->highlighted_path.clear();
    impl_->armed_path.clear();
    invalidate_popup_frame(*this,
                           title_damage_rect(title_rects, allocation(), previous_open_menu));
}

void MenuBar::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    ctx.add_color_rect(a, theme_color("background", Color{0.95F, 0.96F, 0.98F, 1.0F}));
    ctx.add_color_rect({a.x, a.y + a.height - 1, a.width, 1},
                       theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F}));

    const auto text_color = theme_color("text-color", Color{0.15F, 0.15F, 0.15F, 1.0F});
    const auto font = menu_font();
    const auto title_rects =
        menu_title_rects(a, impl_->menus, [&](std::string_view text) { return measure_text(text, font); });
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

        const auto geometry =
            popup_geometry(anchor, items, submenu, [&](std::string_view text) {
                return measure_text(text, font);
            });
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
