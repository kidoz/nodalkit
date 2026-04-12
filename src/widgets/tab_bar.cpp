#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/tab_bar.h>
#include <stdexcept>

namespace nk {

namespace {

FontDescriptor tab_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

} // namespace

struct TabBar::Impl {
    std::vector<std::string> tabs;
    Signal<int> selection_changed;
    int selected_index = -1;
    int hovered_index = -1;
    int armed_index = -1;
};

std::shared_ptr<TabBar> TabBar::create() {
    return std::shared_ptr<TabBar>(new TabBar());
}

TabBar::TabBar() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("tab-bar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::TabList);
}

TabBar::~TabBar() = default;

void TabBar::append_tab(std::string label) {
    impl_->tabs.push_back(std::move(label));
    if (impl_->tabs.size() == 1 && impl_->selected_index < 0) {
        impl_->selected_index = 0;
    }
    queue_layout();
    queue_redraw();
}

void TabBar::insert_tab(std::size_t index, std::string label) {
    if (index > impl_->tabs.size()) {
        throw std::out_of_range("TabBar::insert_tab: index out of range");
    }
    impl_->tabs.insert(impl_->tabs.begin() + static_cast<std::ptrdiff_t>(index), std::move(label));
    if (impl_->selected_index >= static_cast<int>(index)) {
        ++impl_->selected_index;
    }
    if (impl_->tabs.size() == 1 && impl_->selected_index < 0) {
        impl_->selected_index = 0;
    }
    queue_layout();
    queue_redraw();
}

void TabBar::remove_tab(std::size_t index) {
    if (index >= impl_->tabs.size()) {
        throw std::out_of_range("TabBar::remove_tab: index out of range");
    }
    impl_->tabs.erase(impl_->tabs.begin() + static_cast<std::ptrdiff_t>(index));
    if (impl_->tabs.empty()) {
        impl_->selected_index = -1;
        impl_->hovered_index = -1;
        impl_->armed_index = -1;
    } else if (impl_->selected_index >= static_cast<int>(impl_->tabs.size())) {
        impl_->selected_index = static_cast<int>(impl_->tabs.size()) - 1;
    }
    queue_layout();
    queue_redraw();
}

void TabBar::clear_tabs() {
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
    if (index >= impl_->tabs.size()) {
        throw std::out_of_range("TabBar::tab_label: index out of range");
    }
    return impl_->tabs[index];
}

void TabBar::set_tab_label(std::size_t index, std::string label) {
    if (index >= impl_->tabs.size()) {
        throw std::out_of_range("TabBar::set_tab_label: index out of range");
    }
    impl_->tabs[index] = std::move(label);
    queue_layout();
    queue_redraw();
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
    ensure_accessible().set_value(std::string(impl_->tabs[static_cast<std::size_t>(index)]));
    queue_redraw();
    impl_->selection_changed.emit(index);
}

Signal<int>& TabBar::on_selection_changed() {
    return impl_->selection_changed;
}

SizeRequest TabBar::measure(const Constraints& /*constraints*/) const {
    const auto font = tab_font();
    const float min_tab_width = theme_number("min-tab-width", 80.0F);
    const float padding_x = theme_number("padding-x", 16.0F);
    const float h = theme_number("min-height", 40.0F);

    float width = 0.0F;
    for (const auto& label : impl_->tabs) {
        const auto measured = measure_text(label, font);
        width += std::max(min_tab_width, measured.width + padding_x * 2.0F);
    }

    if (impl_->tabs.empty()) {
        width = min_tab_width;
    }

    return {width, h, width, h};
}

bool TabBar::handle_mouse_event(const MouseEvent& event) {
    if (impl_->tabs.empty()) {
        return false;
    }

    if (event.button != 1 && event.type != MouseEvent::Type::Move &&
        event.type != MouseEvent::Type::Enter && event.type != MouseEvent::Type::Leave) {
        return false;
    }

    const auto body = allocation();
    const float count = static_cast<float>(impl_->tabs.size());
    const float tab_width = count > 0.0F ? body.width / count : 0.0F;

    auto index_at = [&](Point point) -> int {
        if (!body.contains(point) || tab_width <= 0.0F) {
            return -1;
        }
        const auto raw = static_cast<int>((point.x - body.x) / tab_width);
        return std::clamp(raw, 0, static_cast<int>(impl_->tabs.size()) - 1);
    };

    const auto point = Point{event.x, event.y};
    const int index = index_at(point);

    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->armed_index = index;
        impl_->hovered_index = index;
        if (index >= 0) {
            grab_focus();
            queue_redraw();
            return true;
        }
        return false;
    case MouseEvent::Type::Release: {
        const bool activated = impl_->armed_index >= 0 && impl_->armed_index == index;
        const int armed = impl_->armed_index;
        impl_->armed_index = -1;
        impl_->hovered_index = index;
        queue_redraw();
        if (activated) {
            set_selected_index(armed);
            return true;
        }
        return false;
    }
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
        if (impl_->hovered_index != index) {
            impl_->hovered_index = index;
            queue_redraw();
        }
        return index >= 0;
    case MouseEvent::Type::Leave:
        if (impl_->hovered_index != -1) {
            impl_->hovered_index = -1;
            queue_redraw();
        }
        return false;
    case MouseEvent::Type::Scroll:
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

void TabBar::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float underline_height = theme_number("underline-height", 2.0F);

    // Background
    ctx.add_color_rect(a, theme_color("background", Color{0.95F, 0.96F, 0.98F, 1.0F}));

    // Bottom border line
    ctx.add_color_rect({a.x, a.y + a.height - 1.0F, a.width, 1.0F},
                       theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F}));

    if (impl_->tabs.empty()) {
        return;
    }

    const float tab_width = a.width / static_cast<float>(impl_->tabs.size());
    const auto font = tab_font();
    const auto accent_color = theme_color("accent-color", Color{0.2F, 0.45F, 0.85F, 1.0F});
    const auto hover_bg = theme_color("hover-background", Color{0.92F, 0.93F, 0.95F, 1.0F});

    for (std::size_t index = 0; index < impl_->tabs.size(); ++index) {
        const auto tab_rect = Rect{
            a.x + tab_width * static_cast<float>(index), a.y, tab_width, a.height};
        const bool selected = static_cast<int>(index) == impl_->selected_index;
        const bool hovered = static_cast<int>(index) == impl_->hovered_index;

        // Tab background
        if (hovered && !selected) {
            ctx.add_color_rect(tab_rect, hover_bg);
        }

        // Tab label
        const auto measured = measure_text(impl_->tabs[index], font);
        const float text_x =
            tab_rect.x + std::max(0.0F, (tab_rect.width - measured.width) * 0.5F);
        const float text_y =
            tab_rect.y + std::max(0.0F, (tab_rect.height - measured.height) * 0.5F);
        ctx.add_text({text_x, text_y},
                     impl_->tabs[index],
                     selected ? theme_color("selected-text-color", Color{0.1F, 0.1F, 0.12F, 1.0F})
                              : theme_color("text-color", Color{0.38F, 0.4F, 0.45F, 1.0F}),
                     font);

        // Selected underline
        if (selected) {
            ctx.add_color_rect(
                {tab_rect.x, a.y + a.height - underline_height, tab_rect.width, underline_height},
                accent_color);
        }
    }

    // Focus ring on the selected tab
    if (has_flag(state_flags(), StateFlags::Focused) && impl_->selected_index >= 0) {
        const auto selected_rect = Rect{
            a.x + tab_width * static_cast<float>(impl_->selected_index),
            a.y,
            tab_width,
            a.height};
        ctx.add_border(selected_rect,
                       theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
                       2.0F,
                       0.0F);
    }
}

} // namespace nk
