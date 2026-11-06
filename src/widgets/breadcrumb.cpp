#include <algorithm>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/breadcrumb.h>
#include <nk/widgets/context_menu.h>
#include <stdexcept>
#include <utility>

namespace nk {

namespace {

FontDescriptor breadcrumb_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

FontDescriptor separator_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

constexpr float kOverflowWidth = 36.0F;
constexpr char kSeparator[] = "\xe2\x80\xba"; // "›"

} // namespace

struct Breadcrumb::Impl {
    std::vector<std::string> segments;
    std::vector<int> overflow_map;
    std::shared_ptr<ContextMenu> overflow_menu;
    Signal<std::size_t> navigate;
    int hovered_index = -1;
    int keyboard_index = -1;
    int armed_index = -1;
    bool overflow_hovered = false;
    bool overflow_armed = false;
};

std::shared_ptr<Breadcrumb> Breadcrumb::create() {
    return std::shared_ptr<Breadcrumb>(new Breadcrumb());
}

Breadcrumb::Breadcrumb() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("breadcrumb");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Navigation);
    accessible.set_name("Breadcrumb");
    impl_->overflow_menu = ContextMenu::create();
    append_child(impl_->overflow_menu);
    (void)impl_->overflow_menu->on_item_activated().connect([this](int index) {
        if (index < 0 || std::cmp_greater_equal(index, impl_->overflow_map.size())) {
            return;
        }
        activate_segment(
            static_cast<std::size_t>(impl_->overflow_map[static_cast<std::size_t>(index)]));
    });
}

Breadcrumb::~Breadcrumb() = default;

void Breadcrumb::set_path(std::vector<std::string> segments) {
    impl_->segments = std::move(segments);
    impl_->hovered_index = -1;
    impl_->keyboard_index = -1;
    impl_->armed_index = -1;
    queue_layout();
    queue_redraw();
}

void Breadcrumb::push(std::string segment) {
    impl_->segments.push_back(std::move(segment));
    impl_->hovered_index = -1;
    clamp_keyboard_index();
    queue_layout();
    queue_redraw();
}

void Breadcrumb::pop_to(std::size_t depth) {
    if (depth < impl_->segments.size()) {
        impl_->segments.resize(depth);
        impl_->hovered_index = -1;
        clamp_keyboard_index();
        queue_layout();
        queue_redraw();
    }
}

std::size_t Breadcrumb::depth() const {
    return impl_->segments.size();
}

std::string_view Breadcrumb::segment(std::size_t index) const {
    if (index >= impl_->segments.size()) {
        throw std::out_of_range("Breadcrumb segment index out of range");
    }
    return impl_->segments[index];
}

Signal<std::size_t>& Breadcrumb::on_navigate() {
    return impl_->navigate;
}

float Breadcrumb::bar_height() const {
    return theme_number("min-height", 32.0F);
}

float Breadcrumb::segment_width(std::string_view text) const {
    return measure_text(text, breadcrumb_font()).width;
}

float Breadcrumb::separator_advance() const {
    const float spacing = theme_number("separator-spacing", 4.0F);
    return spacing + measure_text(kSeparator, separator_font()).width + spacing;
}

int Breadcrumb::clickable_count() const {
    if (impl_->segments.empty()) {
        return 0;
    }
    return static_cast<int>(impl_->segments.size()) - 1;
}

void Breadcrumb::clamp_keyboard_index() {
    if (impl_->keyboard_index >= clickable_count()) {
        impl_->keyboard_index = -1;
    }
}

Breadcrumb::Placement Breadcrumb::placement() const {
    Placement placement;
    const auto bounds = allocation();
    const float height =
        bounds.height > 0.0F ? std::min(bounds.height, bar_height()) : bar_height();
    const float pad = theme_number("padding-x", 8.0F);
    const float separator = separator_advance();

    std::vector<float> widths;
    widths.reserve(impl_->segments.size());
    float natural = pad;
    for (std::size_t index = 0; index < impl_->segments.size(); ++index) {
        const float width = segment_width(impl_->segments[index]);
        widths.push_back(width);
        natural += width;
        if (index + 1 < impl_->segments.size()) {
            natural += separator;
        }
    }
    natural += pad;

    const bool narrow = bounds.width > 0.0F && natural > bounds.width && impl_->segments.size() > 1;
    std::size_t first = 0;
    float overflow_width = 0.0F;
    if (narrow) {
        const float inner = std::max(0.0F, bounds.width - (pad * 2.0F));
        overflow_width = std::min(kOverflowWidth, inner);
        float remaining = std::max(0.0F, inner - overflow_width);
        first = impl_->segments.size() - 1;
        remaining = std::max(0.0F, remaining - std::min(widths.back(), remaining));
        while (first > 0) {
            const float cost = separator + widths[first - 1];
            if (cost > remaining) {
                break;
            }
            remaining -= cost;
            --first;
        }
    }

    float x = bounds.x + pad;
    if (narrow && first > 0) {
        placement.has_overflow = true;
        placement.overflow = {x, bounds.y, overflow_width, height};
        x += overflow_width;
    }
    const float limit = bounds.width > 0.0F ? bounds.right() - pad : x + natural;
    for (std::size_t index = first; index < impl_->segments.size(); ++index) {
        float slot_width = widths[index];
        if (x + slot_width > limit) {
            slot_width = std::max(0.0F, limit - x);
        }
        SegmentSlot slot;
        slot.index = static_cast<int>(index);
        slot.bounds = {x, bounds.y, slot_width, height};
        slot.text_width = widths[index];
        placement.visible.push_back(slot);
        x += slot_width;
        if (index + 1 < impl_->segments.size()) {
            x += separator;
        }
    }
    return placement;
}

void Breadcrumb::activate_segment(std::size_t index) {
    if (index + 1 >= impl_->segments.size()) {
        return;
    }
    const auto keep_alive = shared_from_this();
    (void)keep_alive;
    impl_->navigate.emit(index);
}

void Breadcrumb::open_overflow() {
    const auto placed = placement();
    std::vector<NativeMenuItem> items;
    impl_->overflow_map.clear();
    for (std::size_t index = 0; index < impl_->segments.size(); ++index) {
        const bool visible =
            std::any_of(placed.visible.begin(), placed.visible.end(), [&](const SegmentSlot& slot) {
                return slot.index == static_cast<int>(index);
            });
        if (visible) {
            continue;
        }
        NativeMenuItem item;
        item.label = impl_->segments[index];
        items.push_back(std::move(item));
        impl_->overflow_map.push_back(static_cast<int>(index));
    }
    impl_->overflow_menu->set_items(std::move(items));
    impl_->overflow_menu->show_at({placed.overflow.x, placed.overflow.bottom()});
}

SizeRequest Breadcrumb::measure(const Constraints& /*constraints*/) const {
    const float pad = theme_number("padding-x", 8.0F);
    const float separator = separator_advance();
    float width = pad;
    for (std::size_t index = 0; index < impl_->segments.size(); ++index) {
        width += segment_width(impl_->segments[index]);
        if (index + 1 < impl_->segments.size()) {
            width += separator;
        }
    }
    width += pad;
    const float height = bar_height();
    return {width, height, width, height};
}

bool Breadcrumb::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1 && event.type != MouseEvent::Type::Move &&
        event.type != MouseEvent::Type::Enter && event.type != MouseEvent::Type::Leave) {
        return false;
    }
    const auto placed = placement();
    const Point point{event.x, event.y};
    const bool on_overflow = placed.has_overflow && placed.overflow.contains(point);
    const auto segment_at = [&]() -> int {
        if (impl_->segments.empty()) {
            return -1;
        }
        const int current = static_cast<int>(impl_->segments.size()) - 1;
        for (const auto& slot : placed.visible) {
            if (slot.index != current && slot.bounds.contains(point)) {
                return slot.index;
            }
        }
        return -1;
    };
    const int index = segment_at();

    switch (event.type) {
    case MouseEvent::Type::Press:
        if (!allocation().contains(point)) {
            return false;
        }
        impl_->overflow_armed = on_overflow;
        impl_->armed_index = index;
        impl_->hovered_index = index;
        impl_->overflow_hovered = on_overflow;
        grab_focus();
        queue_redraw();
        return true;
    case MouseEvent::Type::Release: {
        const auto keep_alive = shared_from_this();
        (void)keep_alive;
        const bool overflow = impl_->overflow_armed && on_overflow;
        const int armed = impl_->armed_index;
        impl_->overflow_armed = false;
        impl_->armed_index = -1;
        impl_->hovered_index = index;
        impl_->overflow_hovered = on_overflow;
        queue_redraw();
        if (overflow) {
            open_overflow();
            return true;
        }
        if (armed >= 0 && index == armed) {
            activate_segment(static_cast<std::size_t>(armed));
            return true;
        }
        return false;
    }
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
        if (impl_->hovered_index != index || impl_->overflow_hovered != on_overflow) {
            impl_->hovered_index = index;
            impl_->overflow_hovered = on_overflow;
            queue_redraw();
        }
        return allocation().contains(point);
    case MouseEvent::Type::Leave:
        if (impl_->hovered_index != -1 || impl_->overflow_hovered) {
            impl_->hovered_index = -1;
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

bool Breadcrumb::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }
    const int count = clickable_count();
    if (count <= 0) {
        return false;
    }

    switch (event.key) {
    case KeyCode::Left:
        if (impl_->keyboard_index < 0) {
            impl_->keyboard_index = count - 1;
        } else if (impl_->keyboard_index > 0) {
            --impl_->keyboard_index;
        }
        queue_redraw();
        return true;
    case KeyCode::Right:
        if (impl_->keyboard_index < 0) {
            impl_->keyboard_index = 0;
        } else if (impl_->keyboard_index < count - 1) {
            ++impl_->keyboard_index;
        }
        queue_redraw();
        return true;
    case KeyCode::Home:
        impl_->keyboard_index = 0;
        queue_redraw();
        return true;
    case KeyCode::End:
        impl_->keyboard_index = count - 1;
        queue_redraw();
        return true;
    case KeyCode::Return:
    case KeyCode::Space:
        if (impl_->keyboard_index < 0) {
            return false;
        }
        activate_segment(static_cast<std::size_t>(impl_->keyboard_index));
        return true;
    default:
        return false;
    }
}

bool Breadcrumb::hit_test(Point point) const {
    return Widget::hit_test(point) || impl_->overflow_menu->hit_test(point);
}

std::vector<Rect> Breadcrumb::damage_regions() const {
    auto regions = Widget::damage_regions();
    const auto menu_regions = impl_->overflow_menu->damage_regions();
    regions.insert(regions.end(), menu_regions.begin(), menu_regions.end());
    return regions;
}

CursorShape Breadcrumb::cursor_shape() const {
    return impl_->hovered_index >= 0 || impl_->overflow_hovered ? CursorShape::PointingHand
                                                                : CursorShape::Default;
}

void Breadcrumb::snapshot(SnapshotContext& ctx) const {
    const auto placed = placement();
    const auto font = breadcrumb_font();
    const auto sep_font = separator_font();
    const float separator_spacing = theme_number("separator-spacing", 4.0F);
    const auto text_color = theme_color("text-color");
    const auto link_color = theme_color("link-color", Color{0.2F, 0.4F, 0.8F, 1.0F});
    const auto hover_color = theme_color("hover-color", Color{0.15F, 0.35F, 0.7F, 1.0F});
    const auto sep_color = theme_color("separator-color", Color{0.5F, 0.5F, 0.5F, 1.0F});
    const auto sep_size = measure_text(kSeparator, sep_font);
    const bool focused = has_flag(state_flags(), StateFlags::Focused);
    const int current = impl_->segments.empty() ? -1 : static_cast<int>(impl_->segments.size()) - 1;

    if (placed.has_overflow) {
        const auto mark = measure_text("…", font);
        ctx.add_text({placed.overflow.x + ((placed.overflow.width - mark.width) * 0.5F),
                      placed.overflow.y + ((placed.overflow.height - mark.height) * 0.5F)},
                     "…",
                     theme_color("text-color"),
                     font);
        const bool overflow_focus = focused && impl_->keyboard_index >= 0 &&
                                    std::none_of(placed.visible.begin(),
                                                 placed.visible.end(),
                                                 [&](const SegmentSlot& slot) {
                                                     return slot.index == impl_->keyboard_index;
                                                 });
        if (overflow_focus) {
            ctx.add_border(placed.overflow, theme_color("focus-ring-color"), 2.0F, 0.0F);
        }
    }

    for (std::size_t slot_index = 0; slot_index < placed.visible.size(); ++slot_index) {
        const auto& slot = placed.visible[slot_index];
        const auto& label = impl_->segments[static_cast<std::size_t>(slot.index)];
        const auto measured = measure_text(label, font);
        const float text_y =
            slot.bounds.y + std::max(0.0F, (slot.bounds.height - measured.height) * 0.5F);
        const bool is_last = slot.index == current;
        const bool is_hovered = slot.index == impl_->hovered_index;
        const bool is_keyed = focused && slot.index == impl_->keyboard_index;
        Color color = text_color;
        if (!is_last) {
            color = (is_hovered || is_keyed) ? hover_color : link_color;
        }
        add_text_elided(
            ctx, {slot.bounds.x, text_y}, label, measured, slot.bounds.width, color, font);
        if (is_hovered && !is_last) {
            ctx.add_color_rect({slot.bounds.x,
                                text_y + measured.height,
                                std::min(slot.text_width, slot.bounds.width),
                                1.0F},
                               hover_color);
        }
        if (is_keyed) {
            ctx.add_border(slot.bounds, theme_color("focus-ring-color"), 2.0F, 0.0F);
        }
        if (slot_index + 1 < placed.visible.size()) {
            const float sep_x = slot.bounds.right() + separator_spacing;
            const float sep_y =
                slot.bounds.y + std::max(0.0F, (slot.bounds.height - sep_size.height) * 0.5F);
            ctx.add_text({sep_x, sep_y}, kSeparator, sep_color, sep_font);
        }
    }
    Widget::snapshot(ctx);
}

} // namespace nk
