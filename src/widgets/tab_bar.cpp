#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/context_menu.h>
#include <nk/widgets/tab_bar.h>
#include <stdexcept>
#include <utility>

namespace nk {

namespace {

FontDescriptor tab_font() {
    return FontDescriptor{.family = {}, .size = 13.5F, .weight = FontWeight::Medium};
}

constexpr float kIconSize = 16.0F;
constexpr float kCloseSize = 16.0F;
constexpr float kOverflowWidth = 36.0F;

void add_icon(SnapshotContext& ctx, SymbolicIcon icon, Rect bounds, Color color) {
    const Point center{bounds.x + (bounds.width * 0.5F), bounds.y + (bounds.height * 0.5F)};
    switch (icon) {
    case SymbolicIcon::None:
        break;
    case SymbolicIcon::Search:
        ctx.add_border({center.x - 5.0F, center.y - 6.0F, 8.0F, 8.0F}, color, 1.5F, 4.0F);
        ctx.add_line(
            {center.x + 1.5F, center.y + 1.0F}, {center.x + 5.0F, center.y + 4.5F}, color, 1.5F);
        break;
    case SymbolicIcon::Add:
        ctx.add_line({center.x - 5.0F, center.y}, {center.x + 5.0F, center.y}, color, 1.5F);
        ctx.add_line({center.x, center.y - 5.0F}, {center.x, center.y + 5.0F}, color, 1.5F);
        break;
    case SymbolicIcon::Close:
        ctx.add_line(
            {center.x - 4.0F, center.y - 4.0F}, {center.x + 4.0F, center.y + 4.0F}, color, 1.5F);
        ctx.add_line(
            {center.x + 4.0F, center.y - 4.0F}, {center.x - 4.0F, center.y + 4.0F}, color, 1.5F);
        break;
    }
}

} // namespace

struct TabBar::Impl {
    std::vector<Tab> tabs;
    std::vector<int> overflow_map;
    std::shared_ptr<ContextMenu> overflow_menu;
    Signal<int> selection_changed;
    int selected_index = -1;
    int hovered_index = -1;
    int armed_index = -1;
    bool close_armed = false;
    bool overflow_armed = false;
};

std::shared_ptr<TabBar> TabBar::create() {
    return std::shared_ptr<TabBar>(new TabBar());
}

TabBar::TabBar() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("tab-bar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::TabList);
    impl_->overflow_menu = ContextMenu::create();
    append_child(impl_->overflow_menu);
    (void)impl_->overflow_menu->on_item_activated().connect([this](int index) {
        if (index < 0 || index >= static_cast<int>(impl_->overflow_map.size())) {
            return;
        }
        set_selected_index(impl_->overflow_map[static_cast<std::size_t>(index)]);
    });
}

TabBar::~TabBar() = default;

void TabBar::append_tab(std::string label) {
    insert_tab(impl_->tabs.size(), std::move(label));
}

void TabBar::insert_tab(std::size_t index, std::string label) {
    if (index > impl_->tabs.size()) {
        throw std::out_of_range("TabBar::insert_tab: index out of range");
    }
    impl_->tabs.insert(impl_->tabs.begin() + static_cast<std::ptrdiff_t>(index),
                       Tab{.label = std::move(label),
                           .icon = SymbolicIcon::None,
                           .closable = false,
                           .page = nullptr});
    if (impl_->selected_index >= static_cast<int>(index)) {
        ++impl_->selected_index;
    }
    if (impl_->tabs.size() == 1 && impl_->selected_index < 0) {
        impl_->selected_index = 0;
    }
    sync_pages();
    queue_layout();
    queue_redraw();
}

void TabBar::remove_tab(std::size_t index) {
    if (index >= impl_->tabs.size()) {
        throw std::out_of_range("TabBar::remove_tab: index out of range");
    }
    const int removed = static_cast<int>(index);
    const bool removed_selected = impl_->selected_index == removed;
    if (impl_->tabs[index].page != nullptr) {
        remove_child(*impl_->tabs[index].page);
    }
    impl_->tabs.erase(impl_->tabs.begin() + static_cast<std::ptrdiff_t>(index));
    const int previous = impl_->selected_index;
    if (impl_->tabs.empty()) {
        impl_->selected_index = -1;
        impl_->hovered_index = -1;
        impl_->armed_index = -1;
    } else if (removed_selected) {
        impl_->selected_index = std::min(removed, static_cast<int>(impl_->tabs.size()) - 1);
    } else if (impl_->selected_index > removed) {
        --impl_->selected_index;
    }
    sync_pages();
    queue_layout();
    queue_redraw();
    if (impl_->selected_index != previous || removed_selected) {
        impl_->selection_changed.emit(impl_->selected_index);
    }
}

void TabBar::clear_tabs() {
    for (const auto& tab : impl_->tabs) {
        if (tab.page != nullptr) {
            remove_child(*tab.page);
        }
    }
    impl_->tabs.clear();
    impl_->selected_index = -1;
    impl_->hovered_index = -1;
    impl_->armed_index = -1;
    queue_layout();
    queue_redraw();
}

std::size_t TabBar::tab_count() const {
    return impl_->tabs.size();
}

std::string_view TabBar::tab_label(std::size_t index) const {
    return tab_at(index).label;
}

void TabBar::set_tab_label(std::size_t index, std::string label) {
    tab_at(index).label = std::move(label);
    queue_layout();
    queue_redraw();
}

void TabBar::set_tab_icon(std::size_t index, SymbolicIcon icon) {
    tab_at(index).icon = icon;
    queue_layout();
    queue_redraw();
}

SymbolicIcon TabBar::tab_icon(std::size_t index) const {
    return tab_at(index).icon;
}

void TabBar::set_tab_closable(std::size_t index, bool closable) {
    tab_at(index).closable = closable;
    queue_layout();
    queue_redraw();
}

bool TabBar::is_tab_closable(std::size_t index) const {
    return tab_at(index).closable;
}

void TabBar::set_tab_page(std::size_t index, std::shared_ptr<Widget> page) {
    auto& tab = tab_at(index);
    if (tab.page == page) {
        return;
    }
    if (tab.page != nullptr) {
        remove_child(*tab.page);
    }
    tab.page = std::move(page);
    if (tab.page != nullptr) {
        const auto count = children().size();
        insert_child(count == 0 ? 0 : count - 1, tab.page);
        tab.page->set_visible(static_cast<int>(index) == impl_->selected_index);
    }
    queue_layout();
    queue_redraw();
}

Widget* TabBar::tab_page(std::size_t index) const {
    return tab_at(index).page.get();
}

int TabBar::selected_index() const {
    return impl_->selected_index;
}

void TabBar::set_selected_index(int index) {
    if (index < 0 || index >= static_cast<int>(impl_->tabs.size())) {
        throw std::out_of_range("TabBar::set_selected_index: index out of range");
    }
    if (impl_->selected_index == index) {
        return;
    }
    impl_->selected_index = index;
    ensure_accessible().set_value(std::string(impl_->tabs[static_cast<std::size_t>(index)].label));
    sync_pages();
    queue_redraw();
    impl_->selection_changed.emit(index);
}

Signal<int>& TabBar::on_selection_changed() {
    return impl_->selection_changed;
}

TabBar::Tab& TabBar::tab_at(std::size_t index) {
    if (index >= impl_->tabs.size()) {
        throw std::out_of_range("TabBar: index out of range");
    }
    return impl_->tabs[index];
}

const TabBar::Tab& TabBar::tab_at(std::size_t index) const {
    if (index >= impl_->tabs.size()) {
        throw std::out_of_range("TabBar: index out of range");
    }
    return impl_->tabs[index];
}

void TabBar::sync_pages() {
    for (std::size_t index = 0; index < impl_->tabs.size(); ++index) {
        if (impl_->tabs[index].page != nullptr) {
            impl_->tabs[index].page->set_visible(static_cast<int>(index) == impl_->selected_index);
        }
    }
}

float TabBar::tab_width(const Tab& tab) const {
    const float padding_x = theme_number("padding-x", 16.0F);
    const float min_tab_width = theme_number("min-tab-width", 80.0F);
    float width = measure_text(tab.label, tab_font()).width + (padding_x * 2.0F);
    if (tab.icon != SymbolicIcon::None) {
        width += kIconSize + 6.0F;
    }
    if (tab.closable) {
        width += kCloseSize + 4.0F;
    }
    return std::max(min_tab_width, width);
}

float TabBar::bar_height() const {
    return theme_number("min-height", 40.0F);
}

TabBar::Placement TabBar::placement() const {
    Placement placement;
    const auto bounds = allocation();
    const float height =
        std::min(bar_height(), bounds.height > 0.0F ? bounds.height : bar_height());
    float total = 0.0F;
    std::vector<float> widths;
    widths.reserve(impl_->tabs.size());
    for (const auto& tab : impl_->tabs) {
        const float width = tab_width(tab);
        widths.push_back(width);
        total += width;
    }
    const bool overflow = total > bounds.width && bounds.width > 0.0F;
    float budget = overflow ? std::max(0.0F, bounds.width - kOverflowWidth) : bounds.width;
    float x = bounds.x;
    for (std::size_t index = 0; index < impl_->tabs.size(); ++index) {
        if (overflow && x - bounds.x + widths[index] > budget) {
            break;
        }
        PlacementItem item;
        item.index = static_cast<int>(index);
        item.bounds = {x, bounds.y, widths[index], height};
        if (impl_->tabs[index].closable) {
            item.close = {item.bounds.right() - kCloseSize - 8.0F,
                          item.bounds.y + ((item.bounds.height - kCloseSize) * 0.5F),
                          kCloseSize,
                          kCloseSize};
        }
        placement.visible.push_back(item);
        x += widths[index];
    }
    if (overflow) {
        placement.has_overflow = true;
        const float width = std::min(kOverflowWidth, bounds.width);
        placement.overflow = {bounds.x + bounds.width - width, bounds.y, width, height};
    }
    return placement;
}

void TabBar::open_overflow() {
    const auto placed = placement();
    std::vector<NativeMenuItem> items;
    impl_->overflow_map.clear();
    for (std::size_t index = 0; index < impl_->tabs.size(); ++index) {
        const bool visible = std::any_of(
            placed.visible.begin(), placed.visible.end(), [&](const PlacementItem& item) {
                return item.index == static_cast<int>(index);
            });
        if (visible) {
            continue;
        }
        NativeMenuItem item;
        item.label = impl_->tabs[index].label;
        items.push_back(std::move(item));
        impl_->overflow_map.push_back(static_cast<int>(index));
    }
    impl_->overflow_menu->set_items(std::move(items));
    impl_->overflow_menu->show_at({placed.overflow.x, placed.overflow.bottom()});
}

SizeRequest TabBar::measure(const Constraints& constraints) const {
    float width = 0.0F;
    for (const auto& tab : impl_->tabs) {
        width += tab_width(tab);
    }
    if (impl_->tabs.empty()) {
        width = theme_number("min-tab-width", 80.0F);
    }
    float height = bar_height();
    if (impl_->selected_index >= 0) {
        const auto& page = impl_->tabs[static_cast<std::size_t>(impl_->selected_index)].page;
        if (page != nullptr) {
            height += page->measure_for_diagnostics(constraints).natural_height;
        }
    }
    return {width, height, width, height};
}

void TabBar::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    const float header = std::min(bar_height(), allocation.height);
    for (std::size_t index = 0; index < impl_->tabs.size(); ++index) {
        const auto& page = impl_->tabs[index].page;
        if (page == nullptr) {
            continue;
        }
        const bool selected = static_cast<int>(index) == impl_->selected_index;
        page->set_visible(selected);
        if (selected) {
            page->allocate({allocation.x,
                            allocation.y + header,
                            allocation.width,
                            std::max(0.0F, allocation.height - header)});
        }
    }
}

bool TabBar::handle_mouse_event(const MouseEvent& event) {
    if (impl_->tabs.empty()) {
        return false;
    }
    if (event.button != 1 && event.type != MouseEvent::Type::Move &&
        event.type != MouseEvent::Type::Enter && event.type != MouseEvent::Type::Leave) {
        return false;
    }
    const auto placed = placement();
    const Point point{event.x, event.y};
    const auto visible_at = [&]() -> const PlacementItem* {
        for (const auto& item : placed.visible) {
            if (item.bounds.contains(point)) {
                return &item;
            }
        }
        return nullptr;
    };
    const auto* item = visible_at();
    const bool on_overflow = placed.has_overflow && placed.overflow.contains(point);
    const bool on_close =
        item != nullptr && item->close.width > 0.0F && item->close.contains(point);

    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->close_armed = on_close;
        impl_->overflow_armed = on_overflow;
        impl_->armed_index = item != nullptr && !on_close ? item->index : -1;
        impl_->hovered_index = item != nullptr ? item->index : -1;
        if (item != nullptr || on_overflow) {
            grab_focus();
            queue_redraw();
            return true;
        }
        return false;
    case MouseEvent::Type::Release: {
        const auto keep_alive = shared_from_this();
        (void)keep_alive;
        const bool close = impl_->close_armed && on_close && item != nullptr;
        const bool overflow = impl_->overflow_armed && on_overflow;
        const int armed = impl_->armed_index;
        const int close_index = item != nullptr ? item->index : -1;
        impl_->close_armed = false;
        impl_->overflow_armed = false;
        impl_->armed_index = -1;
        impl_->hovered_index = item != nullptr ? item->index : -1;
        queue_redraw();
        if (close) {
            remove_tab(static_cast<std::size_t>(close_index));
            return true;
        }
        if (overflow) {
            open_overflow();
            return true;
        }
        if (armed >= 0 && item != nullptr && item->index == armed) {
            set_selected_index(armed);
            return true;
        }
        return false;
    }
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
        if (impl_->hovered_index != (item != nullptr ? item->index : -1)) {
            impl_->hovered_index = item != nullptr ? item->index : -1;
            queue_redraw();
        }
        return item != nullptr || on_overflow;
    case MouseEvent::Type::Leave:
        if (impl_->hovered_index != -1) {
            impl_->hovered_index = -1;
            queue_redraw();
        }
        return false;
    case MouseEvent::Type::Scroll:
    case MouseEvent::Type::DragStart:
    case MouseEvent::Type::DragUpdate:
    case MouseEvent::Type::DragEnd:
        return false;
    }
    return false;
}

bool TabBar::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press || impl_->tabs.empty()) {
        return false;
    }

    const auto last_index = static_cast<int>(impl_->tabs.size()) - 1;
    const int current = impl_->selected_index >= 0 ? impl_->selected_index : 0;

    switch (event.key) {
    case KeyCode::Left:
        set_selected_index(current > 0 ? current - 1 : 0);
        return true;
    case KeyCode::Right:
        set_selected_index(current < last_index ? current + 1 : last_index);
        return true;
    case KeyCode::Home:
        set_selected_index(0);
        return true;
    case KeyCode::End:
        set_selected_index(last_index);
        return true;
    case KeyCode::Return:
    case KeyCode::Space:
        if (impl_->selected_index < 0) {
            set_selected_index(0);
        }
        return true;
    default:
        return false;
    }
}

CursorShape TabBar::cursor_shape() const {
    return CursorShape::PointingHand;
}

void TabBar::on_focus_changed(bool focused) {
    if (!focused && (impl_->hovered_index != -1 || impl_->armed_index != -1)) {
        impl_->hovered_index = -1;
        impl_->armed_index = -1;
        queue_redraw();
    }
}

bool TabBar::hit_test(Point point) const {
    return Widget::hit_test(point) || impl_->overflow_menu->hit_test(point);
}

std::vector<Rect> TabBar::damage_regions() const {
    auto regions = Widget::damage_regions();
    const auto menu_regions = impl_->overflow_menu->damage_regions();
    regions.insert(regions.end(), menu_regions.begin(), menu_regions.end());
    return regions;
}

void TabBar::snapshot(SnapshotContext& ctx) const {
    const auto bounds = allocation();
    const float header = std::min(bar_height(), bounds.height);
    const float underline_height = theme_number("underline-height", 2.0F);
    if (header > 0.0F) {
        ctx.add_color_rect({bounds.x, bounds.y, bounds.width, header},
                           theme_color("background", Color{0.95F, 0.96F, 0.98F, 1.0F}));
        ctx.add_color_rect({bounds.x, bounds.y + header - 1.0F, bounds.width, 1.0F},
                           theme_color("border-color"));
    }
    if (impl_->tabs.empty() || header <= 0.0F) {
        Widget::snapshot(ctx);
        return;
    }

    const auto placed = placement();
    const auto font = tab_font();
    const auto accent_color = theme_color("accent-color");
    const auto hover_bg = theme_color("hover-background", Color{0.92F, 0.93F, 0.95F, 1.0F});
    const float padding_x = theme_number("padding-x", 16.0F);

    for (const auto& item : placed.visible) {
        const auto& tab = impl_->tabs[static_cast<std::size_t>(item.index)];
        const bool selected = item.index == impl_->selected_index;
        const bool hovered = item.index == impl_->hovered_index;
        if (hovered && !selected) {
            ctx.add_color_rect(item.bounds, hover_bg);
        }
        const auto color = selected ? theme_color("selected-text-color")
                                    : theme_color("text-color", Color{0.38F, 0.4F, 0.45F, 1.0F});
        float text_x = item.bounds.x + padding_x;
        if (tab.icon != SymbolicIcon::None) {
            add_icon(ctx,
                     tab.icon,
                     {text_x,
                      item.bounds.y + ((item.bounds.height - kIconSize) * 0.5F),
                      kIconSize,
                      kIconSize},
                     color);
            text_x += kIconSize + 6.0F;
        }
        const auto measured = measure_text(tab.label, font);
        const float text_right =
            item.close.width > 0.0F ? item.close.x - 4.0F : item.bounds.right() - padding_x;
        const float text_y =
            item.bounds.y + std::max(0.0F, (item.bounds.height - measured.height) * 0.5F);
        add_text_elided(ctx,
                        {text_x, text_y},
                        tab.label,
                        measured,
                        std::max(0.0F, text_right - text_x),
                        color,
                        font);
        if (item.close.width > 0.0F) {
            add_icon(ctx, SymbolicIcon::Close, item.close, color);
        }
        if (selected) {
            ctx.add_color_rect({item.bounds.x,
                                bounds.y + header - underline_height,
                                item.bounds.width,
                                underline_height},
                               accent_color);
        }
    }

    if (placed.has_overflow) {
        const auto measured = measure_text("…", font);
        ctx.add_text({placed.overflow.x + ((placed.overflow.width - measured.width) * 0.5F),
                      placed.overflow.y + ((placed.overflow.height - measured.height) * 0.5F)},
                     "…",
                     theme_color("text-color"),
                     font);
    }

    if (has_flag(state_flags(), StateFlags::Focused) && impl_->selected_index >= 0) {
        for (const auto& item : placed.visible) {
            if (item.index == impl_->selected_index) {
                ctx.add_border(item.bounds, theme_color("focus-ring-color"), 2.0F, 0.0F);
                break;
            }
        }
    }
    Widget::snapshot(ctx);
}

} // namespace nk
