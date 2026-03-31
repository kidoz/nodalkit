#include <nk/widgets/menu_bar.h>

#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>

#include <algorithm>

namespace nk {

namespace {

FontDescriptor menu_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
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
};

std::shared_ptr<MenuBar> MenuBar::create() {
    return std::shared_ptr<MenuBar>(new MenuBar());
}

MenuBar::MenuBar()
    : impl_(std::make_unique<Impl>()) {
    add_style_class("menu-bar");
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

SizeRequest MenuBar::measure(Constraints const& constraints) const {
    float const h = theme_number("min-height", 32.0F);
    float const w = constraints.max_width;
    return {0, h, w, h};
}

void MenuBar::snapshot(SnapshotContext& ctx) const {
    auto const a = allocation();
    ctx.add_color_rect(
        a, theme_color("background", Color{0.95F, 0.96F, 0.98F, 1.0F}));
    ctx.add_color_rect({a.x, a.y + a.height - 1, a.width, 1},
                       theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F}));

    auto const text_color =
        theme_color("text-color", Color{0.15F, 0.15F, 0.15F, 1.0F});
    float x_offset = 18.0F;
    auto const font = menu_font();
    for (auto const& menu : impl_->menus) {
        auto const measured = measure_text(menu.title, font);
        float const text_y =
            a.y + std::max(0.0F, (a.height - measured.height) * 0.5F);
        ctx.add_text(
            {a.x + x_offset, text_y},
            menu.title,
            text_color,
            font);
        x_offset += measured.width + 22.0F;
    }
}

} // namespace nk
