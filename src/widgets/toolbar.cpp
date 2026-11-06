#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/popover.h>
#include <nk/widgets/separator.h>
#include <nk/widgets/toolbar.h>
#include <utility>

namespace nk {

namespace {

constexpr float kOverflowWidth = 36.0F;

class OverflowColumn : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<OverflowColumn> create() {
        return std::shared_ptr<OverflowColumn>(new OverflowColumn());
    }

    void set_widgets(std::vector<std::shared_ptr<Widget>> widgets) {
        clear_children();
        for (const auto& widget : widgets) {
            append_child(widget);
        }
        widgets_ = std::move(widgets);
        queue_layout();
    }

    void detach(Widget& widget) { remove_child(widget); }

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override {
        float width = 0.0F;
        float height = 0.0F;
        bool any = false;
        for (const auto& widget : widgets_) {
            if (widget == nullptr) {
                continue;
            }
            if (any) {
                height += 4.0F;
            }
            any = true;
            if (dynamic_cast<const Separator*>(widget.get()) != nullptr) {
                width = std::max(width, 80.0F);
                height += 9.0F;
                continue;
            }
            const auto request = widget->measure(constraints);
            width = std::max(width, request.natural_width);
            height += request.natural_height;
        }
        return {width, height, width, height};
    }

    void allocate(const Rect& allocation) override {
        Widget::allocate(allocation);
        float y = allocation.y;
        for (const auto& widget : widgets_) {
            if (widget == nullptr) {
                continue;
            }
            if (dynamic_cast<const Separator*>(widget.get()) != nullptr) {
                widget->allocate({allocation.x, y + 4.0F, allocation.width, 1.0F});
                y += 9.0F;
                continue;
            }
            const auto request = widget->measure(Constraints::unbounded());
            widget->allocate({allocation.x, y, allocation.width, request.natural_height});
            y += request.natural_height + 4.0F;
        }
    }

private:
    OverflowColumn() = default;
    std::vector<std::shared_ptr<Widget>> widgets_;
};

} // namespace

struct Toolbar::Impl {
    std::vector<Item> items;
    std::vector<int> visible;
    std::vector<int> overflow;
    std::shared_ptr<OverflowColumn> column;
    std::shared_ptr<Popover> popover;
    Rect overflow_rect{};
    bool has_overflow = false;
    bool overflow_hovered = false;
    bool overflow_armed = false;
    int keyboard_stop = -1;
};

std::shared_ptr<Toolbar> Toolbar::create() {
    return std::shared_ptr<Toolbar>(new Toolbar());
}

Toolbar::Toolbar() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("toolbar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Toolbar);
    accessible.set_name("toolbar");
    impl_->column = OverflowColumn::create();
    impl_->popover = Popover::create();
    impl_->popover->set_child(impl_->column);
    append_child(impl_->popover);
}

Toolbar::~Toolbar() {
    for (auto& item : impl_->items) {
        restore_focus(item);
    }
}

void Toolbar::restore_focus(Item& item) {
    if (item.widget != nullptr) {
        item.widget->set_focusable(item.focusable);
    }
}

void Toolbar::add_item(std::shared_ptr<Widget> item) {
    Item entry;
    entry.focusable = item != nullptr && item->is_focusable();
    entry.widget = std::move(item);
    append_child(entry.widget);
    if (entry.focusable) {
        entry.widget->set_focusable(false);
    }
    impl_->items.push_back(std::move(entry));
    queue_layout();
}

void Toolbar::add_separator() {
    auto separator = Separator::create(Orientation::Vertical);
    append_child(separator);
    impl_->items.push_back(Item{.widget = std::move(separator), .focusable = false});
    queue_layout();
}

void Toolbar::clear_items() {
    if (impl_->popover->is_open()) {
        impl_->popover->dismiss();
    }
    impl_->column->set_widgets({});
    for (auto& item : impl_->items) {
        restore_focus(item);
        if (item.widget != nullptr && item.widget->parent() == this) {
            remove_child(*item.widget);
        }
    }
    impl_->items.clear();
    impl_->visible.clear();
    impl_->overflow.clear();
    impl_->has_overflow = false;
    impl_->overflow_rect = {};
    impl_->keyboard_stop = -1;
    queue_layout();
    queue_redraw();
}

void Toolbar::sync_overflow(const std::vector<int>& visible, const std::vector<int>& overflow) {
    if (visible == impl_->visible && overflow == impl_->overflow) {
        return;
    }

    std::vector<std::shared_ptr<Widget>> overflow_widgets;
    overflow_widgets.reserve(overflow.size());
    for (const int index : overflow) {
        auto& item = impl_->items[static_cast<std::size_t>(index)];
        restore_focus(item);
        if (item.widget != nullptr && item.widget->parent() == this) {
            remove_child(*item.widget);
        }
        overflow_widgets.push_back(item.widget);
    }
    impl_->column->set_widgets(std::move(overflow_widgets));

    for (const int index : visible) {
        auto& item = impl_->items[static_cast<std::size_t>(index)];
        if (item.widget != nullptr && item.widget->parent() == this) {
            remove_child(*item.widget);
        }
    }
    std::size_t insert_at = children().size();
    for (std::size_t index = 0; index < children().size(); ++index) {
        if (children()[index].get() == impl_->popover.get()) {
            insert_at = index;
            break;
        }
    }
    for (const int index : visible) {
        auto& item = impl_->items[static_cast<std::size_t>(index)];
        if (item.widget == nullptr) {
            continue;
        }
        if (item.focusable) {
            item.widget->set_focusable(false);
        }
        insert_child(insert_at, item.widget);
        ++insert_at;
    }

    impl_->visible = visible;
    impl_->overflow = overflow;
    const auto stops = keyboard_stops();
    if (impl_->keyboard_stop >= static_cast<int>(stops.size())) {
        impl_->keyboard_stop = -1;
    }
}

std::vector<int> Toolbar::keyboard_stops() const {
    std::vector<int> stops;
    for (const int index : impl_->visible) {
        const auto& item = impl_->items[static_cast<std::size_t>(index)];
        if (item.focusable) {
            stops.push_back(index);
        }
    }
    if (impl_->has_overflow) {
        stops.push_back(-1);
    }
    return stops;
}

void Toolbar::open_overflow() {
    if (!impl_->has_overflow) {
        return;
    }
    const auto& rect = impl_->overflow_rect;
    impl_->popover->show_at({rect.x + (rect.width * 0.5F), rect.bottom()});
}

void Toolbar::activate_stop(int stop) {
    if (stop < 0) {
        open_overflow();
        return;
    }
    auto& item = impl_->items[static_cast<std::size_t>(stop)];
    if (item.widget == nullptr) {
        return;
    }
    item.widget->handle_key_event({.type = KeyEvent::Type::Press, .key = KeyCode::Return});
}

SizeRequest Toolbar::measure(const Constraints& constraints) const {
    const float spacing = theme_number("spacing", 8.0F);
    const float padding_x = theme_number("padding-x", 8.0F);
    const float padding_y = theme_number("padding-y", 4.0F);

    float total_width = padding_x * 2.0F;
    float max_height = 0.0F;
    for (std::size_t index = 0; index < impl_->items.size(); ++index) {
        const auto& widget = impl_->items[index].widget;
        if (widget == nullptr || !widget->is_visible()) {
            continue;
        }
        const auto request = widget->measure(constraints);
        total_width += request.natural_width;
        max_height = std::max(max_height, request.natural_height);
        if (index + 1 < impl_->items.size()) {
            total_width += spacing;
        }
    }

    const float height =
        std::max(theme_number("min-height", 40.0F), max_height + (padding_y * 2.0F));
    return {total_width, height, total_width, height};
}

void Toolbar::allocate(const Rect& allocation) {
    Widget::allocate(allocation);

    const float spacing = theme_number("spacing", 8.0F);
    const float padding_x = theme_number("padding-x", 8.0F);
    const float padding_y = theme_number("padding-y", 4.0F);
    const float content_height = std::max(0.0F, allocation.height - (padding_y * 2.0F));

    std::vector<float> widths;
    widths.reserve(impl_->items.size());
    float natural = padding_x * 2.0F;
    for (std::size_t index = 0; index < impl_->items.size(); ++index) {
        const auto& widget = impl_->items[index].widget;
        float width = 0.0F;
        if (widget != nullptr && widget->is_visible()) {
            width = widget->measure(Constraints::unbounded()).natural_width;
            natural += width;
            if (index + 1 < impl_->items.size()) {
                natural += spacing;
            }
        }
        widths.push_back(width);
    }

    std::vector<int> visible;
    std::vector<int> overflow;
    const bool narrow =
        allocation.width > 0.0F && natural > allocation.width && !impl_->items.empty();
    if (!narrow) {
        for (std::size_t index = 0; index < impl_->items.size(); ++index) {
            visible.push_back(static_cast<int>(index));
        }
        impl_->has_overflow = false;
        impl_->overflow_rect = {};
    } else {
        const float inner = std::max(0.0F, allocation.width - (padding_x * 2.0F));
        const float button_width = std::min(kOverflowWidth, inner);
        float remaining = std::max(0.0F, inner - button_width);
        float used = 0.0F;
        std::size_t count = 0;
        for (std::size_t index = 0; index < impl_->items.size(); ++index) {
            if (widths[index] <= 0.0F && impl_->items[index].widget != nullptr &&
                !impl_->items[index].widget->is_visible()) {
                continue;
            }
            const float gap = count == 0 ? 0.0F : spacing;
            if (used + gap + widths[index] > remaining) {
                break;
            }
            used += gap + widths[index];
            ++count;
            visible.push_back(static_cast<int>(index));
        }
        while (!visible.empty()) {
            const auto& last = impl_->items[static_cast<std::size_t>(visible.back())];
            if (dynamic_cast<const Separator*>(last.widget.get()) == nullptr) {
                break;
            }
            visible.pop_back();
        }
        for (std::size_t index = 0; index < impl_->items.size(); ++index) {
            const int item_index = static_cast<int>(index);
            if (std::find(visible.begin(), visible.end(), item_index) == visible.end()) {
                overflow.push_back(item_index);
            }
        }
        impl_->has_overflow = !overflow.empty();
        impl_->overflow_rect = impl_->has_overflow
                                   ? Rect{allocation.right() - padding_x - button_width,
                                          allocation.y + padding_y,
                                          button_width,
                                          content_height}
                                   : Rect{};
    }

    sync_overflow(visible, overflow);
    if (!impl_->has_overflow && impl_->popover->is_open()) {
        impl_->popover->dismiss();
    }

    float x = allocation.x + padding_x;
    const float y = allocation.y + padding_y;
    for (std::size_t index = 0; index < visible.size(); ++index) {
        auto& item = impl_->items[static_cast<std::size_t>(visible[index])];
        if (item.widget == nullptr) {
            continue;
        }
        const float width = widths[static_cast<std::size_t>(visible[index])];
        item.widget->allocate({x, y, width, content_height});
        x += width;
        if (index + 1 < visible.size()) {
            x += spacing;
        }
    }
}

bool Toolbar::handle_mouse_event(const MouseEvent& event) {
    if (!impl_->has_overflow) {
        return false;
    }
    if (event.button != 1 && event.type != MouseEvent::Type::Move &&
        event.type != MouseEvent::Type::Enter && event.type != MouseEvent::Type::Leave) {
        return false;
    }
    const Point point{event.x, event.y};
    const bool on_button = impl_->overflow_rect.contains(point);
    switch (event.type) {
    case MouseEvent::Type::Press:
        impl_->overflow_armed = on_button;
        if (on_button) {
            grab_focus();
            queue_redraw();
        }
        return on_button;
    case MouseEvent::Type::Release: {
        const bool open = impl_->overflow_armed && on_button;
        impl_->overflow_armed = false;
        if (open) {
            open_overflow();
            return true;
        }
        return false;
    }
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
        if (impl_->overflow_hovered != on_button) {
            impl_->overflow_hovered = on_button;
            queue_redraw();
        }
        return on_button;
    case MouseEvent::Type::Leave:
        if (impl_->overflow_hovered) {
            impl_->overflow_hovered = false;
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

bool Toolbar::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }
    const auto stops = keyboard_stops();
    if (stops.empty()) {
        return false;
    }
    const int last = static_cast<int>(stops.size()) - 1;
    switch (event.key) {
    case KeyCode::Left:
        if (impl_->keyboard_stop < 0) {
            impl_->keyboard_stop = last;
        } else if (impl_->keyboard_stop > 0) {
            --impl_->keyboard_stop;
        }
        grab_focus();
        queue_redraw();
        return true;
    case KeyCode::Right:
        if (impl_->keyboard_stop < 0) {
            impl_->keyboard_stop = 0;
        } else if (impl_->keyboard_stop < last) {
            ++impl_->keyboard_stop;
        }
        grab_focus();
        queue_redraw();
        return true;
    case KeyCode::Home:
        impl_->keyboard_stop = 0;
        grab_focus();
        queue_redraw();
        return true;
    case KeyCode::End:
        impl_->keyboard_stop = last;
        grab_focus();
        queue_redraw();
        return true;
    case KeyCode::Return:
    case KeyCode::Space:
        if (impl_->keyboard_stop < 0 || impl_->keyboard_stop > last) {
            return false;
        }
        activate_stop(stops[static_cast<std::size_t>(impl_->keyboard_stop)]);
        return true;
    default:
        return false;
    }
}

bool Toolbar::hit_test(Point point) const {
    return Widget::hit_test(point) || impl_->popover->hit_test(point);
}

std::vector<Rect> Toolbar::damage_regions() const {
    auto regions = Widget::damage_regions();
    const auto popup = impl_->popover->damage_regions();
    regions.insert(regions.end(), popup.begin(), popup.end());
    return regions;
}

CursorShape Toolbar::cursor_shape() const {
    return impl_->overflow_hovered ? CursorShape::PointingHand : CursorShape::Default;
}

void Toolbar::snapshot(SnapshotContext& ctx) const {
    const auto bounds = allocation();
    ctx.add_color_rect(bounds, theme_color("background", Color{0.95F, 0.96F, 0.98F, 1.0F}));
    ctx.add_color_rect({bounds.x, bounds.bottom() - 1.0F, bounds.width, 1.0F},
                       theme_color("border-color"));

    if (impl_->has_overflow) {
        const auto font = FontDescriptor{.family = {}, .size = 13.5F, .weight = FontWeight::Medium};
        const auto mark = measure_text("…", font);
        const auto& rect = impl_->overflow_rect;
        ctx.add_text({rect.x + ((rect.width - mark.width) * 0.5F),
                      rect.y + ((rect.height - mark.height) * 0.5F)},
                     "…",
                     theme_color("text-color"),
                     font);
    }

    if (has_flag(state_flags(), StateFlags::Focused)) {
        const auto stops = keyboard_stops();
        if (impl_->keyboard_stop >= 0 && impl_->keyboard_stop < static_cast<int>(stops.size())) {
            const int stop = stops[static_cast<std::size_t>(impl_->keyboard_stop)];
            const Rect ring =
                stop < 0 ? impl_->overflow_rect
                         : impl_->items[static_cast<std::size_t>(stop)].widget->allocation();
            ctx.add_border(ring, theme_color("focus-ring-color"), 2.0F, 0.0F);
        }
    }

    Widget::snapshot(ctx);
}

} // namespace nk
