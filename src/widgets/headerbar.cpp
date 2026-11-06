#include <algorithm>
#include <cctype>
#include <nk/accessibility/accessible.h>
#include <nk/foundation/signal.h>
#include <nk/layout/constraints.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/platform/system_preferences.h>
#include <nk/platform/window.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/ui_core/state_flags.h>
#include <nk/widgets/headerbar.h>
#include <nk/widgets/popover.h>
#include <string>
#include <utility>
#include <vector>

namespace nk {

namespace {

constexpr std::string_view DefaultDecorationLayout = ":minimize,maximize,close";

enum class HeaderbarIcon {
    Back,
    Minimize,
    Maximize,
    Restore,
    Close,
};

enum class WindowControlKind {
    Minimize,
    Maximize,
    Close,
};

struct DecorationLayout {
    std::vector<WindowControlKind> start;
    std::vector<WindowControlKind> end;
};

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

std::vector<WindowControlKind> parse_control_side(std::string_view value) {
    std::vector<WindowControlKind> controls;
    while (!value.empty()) {
        const auto separator = value.find(',');
        const auto name = trim(value.substr(0, separator));
        auto append_unique = [&](WindowControlKind kind) {
            if (std::find(controls.begin(), controls.end(), kind) == controls.end()) {
                controls.push_back(kind);
            }
        };
        if (name == "minimize") {
            append_unique(WindowControlKind::Minimize);
        } else if (name == "maximize") {
            append_unique(WindowControlKind::Maximize);
        } else if (name == "close") {
            append_unique(WindowControlKind::Close);
        }
        if (separator == std::string_view::npos) {
            break;
        }
        value.remove_prefix(separator + 1);
    }
    return controls;
}

DecorationLayout parse_decoration_layout(std::string_view value) {
    if (value.empty()) {
        value = DefaultDecorationLayout;
    }
    const auto separator = value.find(':');
    if (separator == std::string_view::npos) {
        return {{}, parse_control_side(value)};
    }
    return {
        parse_control_side(value.substr(0, separator)),
        parse_control_side(value.substr(separator + 1)),
    };
}

FontDescriptor headerbar_title_font(float size) {
    return FontDescriptor{
        .family = {},
        .size = size,
        .weight = FontWeight::Bold,
    };
}

FontDescriptor tooltip_font(float size) {
    return FontDescriptor{
        .family = {},
        .size = size,
        .weight = FontWeight::Regular,
    };
}

class HeaderbarControl final : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<HeaderbarControl>
    create(HeaderbarIcon icon, std::string accessible_name, bool destructive = false) {
        return std::shared_ptr<HeaderbarControl>(
            new HeaderbarControl(icon, std::move(accessible_name), destructive));
    }

    void set_presentation(HeaderbarIcon icon, std::string accessible_name) {
        if (icon_ == icon && accessible() != nullptr && accessible()->name() == accessible_name) {
            return;
        }
        preserve_damage_regions_for_next_redraw();
        icon_ = icon;
        auto& accessible = ensure_accessible();
        accessible.set_name(accessible_name);
        accessible.set_description(std::move(accessible_name));
        queue_redraw();
    }

    Signal<>& on_clicked() { return clicked_; }

    [[nodiscard]] SizeRequest measure(const Constraints& /*constraints*/) const override {
        const float target = theme_number("target-size", 46.0F);
        return {target, target, target, target};
    }

    bool handle_mouse_event(const MouseEvent& event) override {
        switch (event.type) {
        case MouseEvent::Type::Press:
            if (event.button != 1) {
                return false;
            }
            armed_ = allocation().contains({event.x, event.y});
            return armed_;
        case MouseEvent::Type::Release: {
            if (event.button != 1) {
                return false;
            }
            const bool activate = armed_ && allocation().contains({event.x, event.y});
            armed_ = false;
            if (activate) {
                clicked_.emit();
            }
            return activate;
        }
        case MouseEvent::Type::Enter:
        case MouseEvent::Type::Leave:
        case MouseEvent::Type::Move:
        case MouseEvent::Type::Scroll:
        case MouseEvent::Type::DragStart:
        case MouseEvent::Type::DragUpdate:
        case MouseEvent::Type::DragEnd:
            return false;
        }
        return false;
    }

    bool handle_key_event(const KeyEvent& event) override {
        if (event.type == KeyEvent::Type::Press &&
            (event.key == KeyCode::Space || event.key == KeyCode::Return)) {
            clicked_.emit();
            return true;
        }
        return false;
    }

    [[nodiscard]] CursorShape cursor_shape() const override { return CursorShape::PointingHand; }

    [[nodiscard]] std::vector<Rect> damage_regions() const override {
        auto regions = Widget::damage_regions();
        if (has_flag(state_flags(), StateFlags::Hovered)) {
            regions.push_back(tooltip_bounds());
        }
        return regions;
    }

protected:
    void snapshot(SnapshotContext& ctx) const override {
        const auto rect = allocation();
        const float control_size =
            std::min(theme_number("control-size", 34.0F), std::min(rect.width, rect.height));
        const Rect body{
            rect.x + ((rect.width - control_size) * 0.5F),
            rect.y + ((rect.height - control_size) * 0.5F),
            control_size,
            control_size,
        };
        const bool active = has_flag(state_flags(), StateFlags::Hovered) ||
                            has_flag(state_flags(), StateFlags::Pressed);
        if (active || destructive_) {
            const auto fill =
                destructive_ && active
                    ? theme_color("destructive", Color::from_rgb(230, 45, 66))
                    : theme_color("close-background",
                                  theme_color("surface-hover", Color::from_rgb(240, 243, 247)));
            ctx.add_rounded_rect(body, fill, body.height * 0.5F);
        }
        if (has_flag(state_flags(), StateFlags::FocusVisible)) {
            ctx.add_border(body,
                           theme_color("focus-ring-color", Color::from_rgb(53, 132, 228)),
                           2.0F,
                           body.height * 0.5F);
        }

        auto foreground =
            theme_color("headerbar-fg", theme_color("text-primary", Color::from_rgb(37, 40, 46)));
        if (destructive_ && active) {
            foreground = Color::from_rgb(255, 255, 255);
        }
        const float icon_size = std::min(theme_number("icon-size", 12.0F), control_size * 0.5F);
        const Rect icon_bounds{
            body.x + ((body.width - icon_size) * 0.5F),
            body.y + ((body.height - icon_size) * 0.5F),
            icon_size,
            icon_size,
        };
        draw_symbolic_icon(ctx, icon_bounds, foreground, 1.5F);

        if (has_flag(state_flags(), StateFlags::Hovered) && !armed_) {
            const auto tooltip = tooltip_bounds();
            ctx.push_overlay_container(tooltip);
            ctx.add_rounded_rect(
                tooltip,
                theme_color("tooltip-background",
                            theme_color("surface-osd", Color{0.15F, 0.15F, 0.17F, 0.95F})),
                theme_number("tooltip-radius", 6.0F));
            const auto font = tooltip_font(theme_number("tooltip-font-size", 12.0F));
            const auto name = std::string(accessible()->name());
            const auto measured = measure_text(name, font);
            ctx.add_text({tooltip.x + ((tooltip.width - measured.width) * 0.5F),
                          tooltip.y + ((tooltip.height - measured.height) * 0.5F)},
                         name,
                         theme_color("tooltip-text",
                                     theme_color("text-osd", Color::from_rgb(255, 255, 255))),
                         font);
            ctx.pop_container();
        }
    }

private:
    HeaderbarControl(HeaderbarIcon icon, std::string accessible_name, bool destructive)
        : icon_(icon), destructive_(destructive) {
        set_focusable(true);
        add_style_class("headerbar-control");
        if (destructive_) {
            add_style_class("destructive");
        }
        auto& accessible = ensure_accessible();
        accessible.set_role(AccessibleRole::Button);
        accessible.set_name(accessible_name);
        accessible.set_description(std::move(accessible_name));
        accessible.add_action(AccessibleAction::Activate, [this] {
            clicked_.emit();
            return true;
        });
    }

    void draw_symbolic_icon(SnapshotContext& ctx, Rect bounds, Color color, float thickness) const {
        const float center_y = bounds.y + (bounds.height * 0.5F);
        switch (icon_) {
        case HeaderbarIcon::Back:
        case HeaderbarIcon::Close: {
            const std::string_view glyph = icon_ == HeaderbarIcon::Back ? "‹" : "×";
            const auto font = FontDescriptor{
                .family = {},
                .size = theme_number("symbol-font-size", 18.0F),
                .weight = FontWeight::Regular,
            };
            const auto measured = measure_text(glyph, font);
            ctx.add_text({bounds.x + ((bounds.width - measured.width) * 0.5F),
                          bounds.y + ((bounds.height - measured.height) * 0.5F)},
                         std::string(glyph),
                         color,
                         font);
            break;
        }
        case HeaderbarIcon::Minimize:
            ctx.add_rounded_rect({bounds.x, center_y - (thickness * 0.5F), bounds.width, thickness},
                                 color,
                                 thickness * 0.5F);
            break;
        case HeaderbarIcon::Maximize:
            ctx.add_border(bounds, color, thickness, 1.0F);
            break;
        case HeaderbarIcon::Restore:
            ctx.add_border({bounds.x + 2.0F, bounds.y, bounds.width - 2.0F, bounds.height - 2.0F},
                           color,
                           thickness,
                           1.0F);
            ctx.add_border({bounds.x, bounds.y + 2.0F, bounds.width - 2.0F, bounds.height - 2.0F},
                           color,
                           thickness,
                           1.0F);
            break;
        }
    }

    [[nodiscard]] Rect tooltip_bounds() const {
        const auto font = tooltip_font(theme_number("tooltip-font-size", 12.0F));
        const auto measured =
            measure_text(accessible() != nullptr ? accessible()->name() : "", font);
        const float width = measured.width + 16.0F;
        const float height = measured.height + 8.0F;
        float x = allocation().x + ((allocation().width - width) * 0.5F);
        if (const auto* window = host_window(); window != nullptr) {
            x = std::clamp(x, 4.0F, std::max(4.0F, window->size().width - width - 4.0F));
        }
        return {x, allocation().bottom() + 6.0F, width, height};
    }

    HeaderbarIcon icon_ = HeaderbarIcon::Close;
    bool destructive_ = false;
    bool armed_ = false;
    Signal<> clicked_;
};

constexpr float kOverflowWidth = 36.0F;

class OverflowColumn : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<OverflowColumn> create() {
        return std::shared_ptr<OverflowColumn>(new OverflowColumn());
    }

    void set_widgets(std::vector<std::shared_ptr<Widget>> widgets) {
        clear_children();
        for (const auto& widget : widgets) {
            if (widget != nullptr) {
                append_child(widget);
            }
        }
        widgets_ = std::move(widgets);
    }

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

struct Headerbar::Impl {
    std::string title;
    std::string subtitle;
    std::string decoration_layout;
    HeaderbarCenteringPolicy centering_policy = HeaderbarCenteringPolicy::Strict;
    std::vector<std::shared_ptr<Widget>> leading;
    std::vector<std::shared_ptr<Widget>> trailing;
    std::shared_ptr<HeaderbarControl> back;
    std::shared_ptr<HeaderbarControl> minimize;
    std::shared_ptr<HeaderbarControl> maximize;
    std::shared_ptr<HeaderbarControl> close;
    std::shared_ptr<Widget> title_widget;
    ScopedConnection back_connection;
    ScopedConnection minimize_connection;
    ScopedConnection maximize_connection;
    ScopedConnection close_connection;
    Signal<> back_requested;
    std::shared_ptr<OverflowColumn> overflow_column;
    std::shared_ptr<Popover> overflow_popover;
    std::vector<int> overflow_indices;
    Rect overflow_rect{};
    Rect title_bounds{};
    bool window_controls_enabled = true;
    bool show_back_button = false;
    bool has_trailing_overflow = false;
    bool overflow_armed = false;
};

std::shared_ptr<Headerbar> Headerbar::create(std::string title) {
    return std::shared_ptr<Headerbar>(new Headerbar(std::move(title)));
}

Headerbar::Headerbar(std::string title) : impl_(std::make_unique<Impl>()) {
    impl_->title = std::move(title);
    impl_->back = HeaderbarControl::create(HeaderbarIcon::Back, "Back");
    impl_->minimize = HeaderbarControl::create(HeaderbarIcon::Minimize, "Minimize");
    impl_->maximize = HeaderbarControl::create(HeaderbarIcon::Maximize, "Maximize");
    impl_->close = HeaderbarControl::create(HeaderbarIcon::Close, "Close", true);

    append_child(impl_->back);
    append_child(impl_->minimize);
    append_child(impl_->maximize);
    append_child(impl_->close);
    impl_->overflow_column = OverflowColumn::create();
    impl_->overflow_popover = Popover::create();
    impl_->overflow_popover->set_child(impl_->overflow_column);
    append_child(impl_->overflow_popover);

    impl_->back_connection = ScopedConnection(
        impl_->back->on_clicked().connect([this] { impl_->back_requested.emit(); }));
    impl_->minimize_connection = ScopedConnection(impl_->minimize->on_clicked().connect([this] {
        if (auto* window = host_window(); window != nullptr) {
            window->minimize();
        }
    }));
    impl_->maximize_connection = ScopedConnection(impl_->maximize->on_clicked().connect([this] {
        if (auto* window = host_window(); window != nullptr) {
            window->toggle_maximize();
        }
    }));
    impl_->close_connection = ScopedConnection(impl_->close->on_clicked().connect([this] {
        if (auto* window = host_window(); window != nullptr) {
            window->request_close();
        }
    }));

    add_style_class("headerbar");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Group);
    accessible.set_name(impl_->title.empty() ? "Window Header Bar" : impl_->title);
}

Headerbar::~Headerbar() = default;

std::string_view Headerbar::title() const {
    return impl_->title;
}

void Headerbar::set_title(std::string title) {
    if (impl_->title == title) {
        return;
    }
    impl_->title = std::move(title);
    ensure_accessible().set_name(impl_->title.empty() ? "Window Header Bar" : impl_->title);
    queue_layout();
}

std::string_view Headerbar::subtitle() const {
    return impl_->subtitle;
}

void Headerbar::set_title_widget(std::shared_ptr<Widget> widget) {
    if (impl_->title_widget == widget) {
        return;
    }
    if (impl_->title_widget != nullptr) {
        remove_child(*impl_->title_widget);
    }
    impl_->title_widget = std::move(widget);
    if (impl_->title_widget != nullptr) {
        append_child(impl_->title_widget);
    }
    queue_layout();
}

Widget* Headerbar::title_widget() const {
    return impl_->title_widget.get();
}

void Headerbar::set_subtitle(std::string subtitle) {
    if (impl_->subtitle == subtitle) {
        return;
    }
    impl_->subtitle = std::move(subtitle);
    ensure_accessible().set_description(impl_->subtitle);
    queue_layout();
}

void Headerbar::add_leading(std::shared_ptr<Widget> item) {
    if (item == nullptr) {
        return;
    }
    impl_->leading.push_back(item);
    insert_child(impl_->leading.size(), std::move(item));
}

void Headerbar::add_trailing(std::shared_ptr<Widget> item) {
    if (item == nullptr) {
        return;
    }
    impl_->trailing.push_back(item);
    insert_child(impl_->leading.size() + impl_->trailing.size(), std::move(item));
}

void Headerbar::set_window_controls_enabled(bool enabled) {
    if (impl_->window_controls_enabled == enabled) {
        return;
    }
    impl_->window_controls_enabled = enabled;
    queue_layout();
}

std::string_view Headerbar::decoration_layout() const {
    return impl_->decoration_layout;
}

void Headerbar::set_decoration_layout(std::string layout) {
    if (impl_->decoration_layout == layout) {
        return;
    }
    impl_->decoration_layout = std::move(layout);
    queue_layout();
}

HeaderbarCenteringPolicy Headerbar::centering_policy() const {
    return impl_->centering_policy;
}

void Headerbar::set_centering_policy(HeaderbarCenteringPolicy policy) {
    if (impl_->centering_policy == policy) {
        return;
    }
    impl_->centering_policy = policy;
    queue_layout();
}

bool Headerbar::shows_back_button() const {
    return impl_->show_back_button;
}

void Headerbar::set_show_back_button(bool show) {
    if (impl_->show_back_button == show) {
        return;
    }
    impl_->show_back_button = show;
    queue_layout();
}

Signal<>& Headerbar::on_back_requested() {
    return impl_->back_requested;
}

SizeRequest Headerbar::measure(const Constraints& constraints) const {
    if (const auto* window = host_window();
        window != nullptr && !window->uses_client_side_decorations()) {
        return {};
    }

    const float height = theme_number("headerbar-height", 46.0F);
    const float target = theme_number("headerbar-control-target", 46.0F);
    const float padding = theme_number("headerbar-padding-x", 6.0F);
    const float spacing = theme_number("spacing", 6.0F);
    std::string layout_text = impl_->decoration_layout;
    if (layout_text.empty()) {
        if (const auto* window = host_window(); window != nullptr) {
            layout_text = window->system_preferences().window_decoration_layout.value_or(
                std::string(DefaultDecorationLayout));
        } else {
            layout_text = DefaultDecorationLayout;
        }
    }
    const auto layout = parse_decoration_layout(layout_text);

    float natural_width = padding * 2.0F;
    if (impl_->window_controls_enabled) {
        natural_width += target * static_cast<float>(layout.start.size() + layout.end.size());
    }
    if (impl_->show_back_button) {
        natural_width += target;
    }
    for (const auto& child : impl_->leading) {
        natural_width += child->measure(constraints).natural_width + spacing;
    }
    for (const auto& child : impl_->trailing) {
        natural_width += child->measure(constraints).natural_width + spacing;
    }
    const float heading_width = centered_content_width();
    if (heading_width > 0.0F) {
        natural_width += heading_width + spacing;
    }
    return {0.0F, height, natural_width, height};
}

void Headerbar::sync_trailing_overflow(const std::vector<int>& overflow) {
    if (overflow == impl_->overflow_indices) {
        return;
    }
    std::vector<std::shared_ptr<Widget>> overflow_widgets;
    overflow_widgets.reserve(overflow.size());
    for (const int index : overflow) {
        auto& item = impl_->trailing[static_cast<std::size_t>(index)];
        if (item != nullptr && item->parent() == this) {
            remove_child(*item);
        }
        overflow_widgets.push_back(item);
    }
    impl_->overflow_column->set_widgets(std::move(overflow_widgets));

    std::size_t insert_at = children().size();
    for (std::size_t index = 0; index < children().size(); ++index) {
        if (children()[index].get() == impl_->overflow_popover.get()) {
            insert_at = index;
            break;
        }
    }
    for (std::size_t index = 0; index < impl_->trailing.size(); ++index) {
        if (std::find(overflow.begin(), overflow.end(), static_cast<int>(index)) !=
            overflow.end()) {
            continue;
        }
        auto& item = impl_->trailing[index];
        if (item != nullptr && item->parent() != this) {
            insert_child(insert_at, item);
            ++insert_at;
        }
    }
    impl_->overflow_indices = overflow;
}

void Headerbar::open_trailing_overflow() {
    if (!impl_->has_trailing_overflow) {
        return;
    }
    const auto& rect = impl_->overflow_rect;
    impl_->overflow_popover->show_at({rect.x + (rect.width * 0.5F), rect.bottom()});
}

float Headerbar::centered_content_width() const {
    if (impl_->title_widget != nullptr) {
        return impl_->title_widget->measure(Constraints::unbounded()).natural_width;
    }
    const auto title_text = impl_->title.empty() && host_window() != nullptr
                                ? std::string(host_window()->title())
                                : impl_->title;
    if (title_text.empty() && impl_->subtitle.empty()) {
        return 0.0F;
    }
    float heading_width =
        measure_text(title_text, headerbar_title_font(theme_number("title-font-size", 14.0F)))
            .width;
    if (!impl_->subtitle.empty()) {
        heading_width =
            std::max(heading_width,
                     measure_text(impl_->subtitle,
                                  headerbar_title_font(theme_number("subtitle-font-size", 11.0F)))
                         .width);
    }
    return heading_width;
}

void Headerbar::allocate(const Rect& allocation) {
    Widget::allocate(allocation);

    const bool active = host_window() == nullptr || host_window()->uses_client_side_decorations();
    const bool show_controls = active && impl_->window_controls_enabled;
    const float height = theme_number("headerbar-height", 46.0F);
    const float target = theme_number("headerbar-control-target", 46.0F);
    const float padding = theme_number("headerbar-padding-x", 6.0F);
    const float spacing = theme_number("spacing", 6.0F);

    std::string layout_text = impl_->decoration_layout;
    if (layout_text.empty()) {
        if (const auto* window = host_window(); window != nullptr) {
            layout_text = window->system_preferences().window_decoration_layout.value_or(
                std::string(DefaultDecorationLayout));
        } else {
            layout_text = DefaultDecorationLayout;
        }
    }
    const auto layout = parse_decoration_layout(layout_text);
    auto control_for = [&](WindowControlKind kind) -> std::shared_ptr<HeaderbarControl> {
        switch (kind) {
        case WindowControlKind::Minimize:
            return impl_->minimize;
        case WindowControlKind::Maximize:
            return impl_->maximize;
        case WindowControlKind::Close:
            return impl_->close;
        }
        return impl_->close;
    };
    auto included = [&](WindowControlKind kind) {
        return std::find(layout.start.begin(), layout.start.end(), kind) != layout.start.end() ||
               std::find(layout.end.begin(), layout.end.end(), kind) != layout.end.end();
    };

    impl_->minimize->set_visible(show_controls && included(WindowControlKind::Minimize));
    impl_->maximize->set_visible(show_controls && included(WindowControlKind::Maximize));
    impl_->close->set_visible(show_controls && included(WindowControlKind::Close));
    impl_->back->set_visible(active && impl_->show_back_button);
    if (!active) {
        impl_->title_bounds = {};
        return;
    }

    if (auto* window = host_window(); window != nullptr) {
        impl_->maximize->set_presentation(window->is_maximized() ? HeaderbarIcon::Restore
                                                                 : HeaderbarIcon::Maximize,
                                          window->is_maximized() ? "Restore" : "Maximize");
    }

    const Constraints row_constraints{0.0F, 0.0F, allocation.width, height};
    float leading_x = allocation.x + padding;
    if (show_controls) {
        for (const auto kind : layout.start) {
            auto control = control_for(kind);
            control->allocate({leading_x, allocation.y, target, height});
            leading_x += target;
        }
    }
    if (impl_->back->is_visible()) {
        impl_->back->allocate({leading_x, allocation.y, target, height});
        leading_x += target + spacing;
    } else if (!layout.start.empty() && show_controls) {
        leading_x += spacing;
    }
    for (const auto& child : impl_->leading) {
        const auto request = child->measure(row_constraints);
        const float width =
            std::min(request.natural_width, std::max(0.0F, allocation.right() - leading_x));
        const float child_height = std::min(request.natural_height, height);
        const float y = allocation.y + ((height - child_height) * 0.5F);
        child->allocate({leading_x, y, width, child_height});
        leading_x += width + spacing;
    }

    float trailing_x = allocation.right() - padding;
    if (show_controls) {
        for (auto iterator = layout.end.rbegin(); iterator != layout.end.rend(); ++iterator) {
            trailing_x -= target;
            control_for(*iterator)->allocate({trailing_x, allocation.y, target, height});
        }
    }
    if (!layout.end.empty() && show_controls) {
        trailing_x -= spacing;
    }
    std::vector<float> trailing_widths;
    trailing_widths.reserve(impl_->trailing.size());
    float trailing_total = 0.0F;
    for (const auto& child : impl_->trailing) {
        const float width = child->measure(Constraints::unbounded()).natural_width;
        trailing_widths.push_back(width);
        trailing_total += width + spacing;
    }
    const float title_reserve = centered_content_width();
    const float inner_left = leading_x + (title_reserve > 0.0F ? title_reserve + spacing : 0.0F);
    const float available = trailing_x - inner_left;
    const bool overflow =
        !impl_->trailing.empty() && allocation.width > 0.0F && trailing_total > available;
    std::vector<int> overflow_indices;
    if (!overflow) {
        for (int index = static_cast<int>(impl_->trailing.size()) - 1; index >= 0; --index) {
            auto& child = impl_->trailing[static_cast<std::size_t>(index)];
            const float width = std::min(trailing_widths[static_cast<std::size_t>(index)],
                                         std::max(0.0F, trailing_x - allocation.x));
            const auto request = child->measure(Constraints::unbounded());
            const float child_height = std::min(request.natural_height, height);
            trailing_x -= width;
            const float y = allocation.y + ((height - child_height) * 0.5F);
            child->allocate({trailing_x, y, width, child_height});
            trailing_x -= spacing;
        }
        impl_->has_trailing_overflow = false;
        impl_->overflow_rect = {};
    } else {
        const float gap = std::max(0.0F, trailing_x - leading_x);
        const float button_width = std::min(kOverflowWidth, gap);
        const float item_budget = std::max(0.0F, available - button_width - spacing);
        std::vector<int> visible;
        float used = 0.0F;
        for (int index = static_cast<int>(impl_->trailing.size()) - 1; index >= 0; --index) {
            const float width = trailing_widths[static_cast<std::size_t>(index)];
            const float need = width + (used > 0.0F ? spacing : 0.0F);
            if (used + need > item_budget) {
                break;
            }
            used += need;
            visible.push_back(index);
        }
        for (int index = 0; index < static_cast<int>(impl_->trailing.size()); ++index) {
            if (std::find(visible.begin(), visible.end(), index) == visible.end()) {
                overflow_indices.push_back(index);
            }
        }
        for (const int index : visible) {
            auto& child = impl_->trailing[static_cast<std::size_t>(index)];
            const float width = trailing_widths[static_cast<std::size_t>(index)];
            const auto request = child->measure(Constraints::unbounded());
            const float child_height = std::min(request.natural_height, height);
            trailing_x -= width;
            const float y = allocation.y + ((height - child_height) * 0.5F);
            child->allocate({trailing_x, y, width, child_height});
            trailing_x -= spacing;
        }
        trailing_x -= button_width;
        const float y = allocation.y;
        impl_->overflow_rect = {trailing_x, y, button_width, height};
        impl_->has_trailing_overflow = button_width > 0.0F;
        trailing_x -= spacing;
    }
    sync_trailing_overflow(overflow_indices);
    if (!impl_->has_trailing_overflow && impl_->overflow_popover->is_open()) {
        impl_->overflow_popover->dismiss();
    }

    if (impl_->centering_policy == HeaderbarCenteringPolicy::Strict) {
        const float start_extent = leading_x - allocation.x;
        const float end_extent = allocation.right() - trailing_x;
        const float symmetric_extent = std::max(start_extent, end_extent);
        const Rect strict_bounds{
            allocation.x + symmetric_extent,
            allocation.y,
            std::max(0.0F, allocation.width - (symmetric_extent * 2.0F)),
            height,
        };
        const float title_width = centered_content_width();
        // Preserve strict window-relative centering whenever the title fits.
        // At narrow widths, fall back to the full free span instead of
        // needlessly eliding a title because one side contains more controls.
        impl_->title_bounds =
            strict_bounds.width >= title_width
                ? strict_bounds
                : Rect{leading_x, allocation.y, std::max(0.0F, trailing_x - leading_x), height};
    } else {
        impl_->title_bounds = {
            leading_x,
            allocation.y,
            std::max(0.0F, trailing_x - leading_x),
            height,
        };
    }

    if (impl_->title_widget != nullptr) {
        const bool hide_title =
            host_window() != nullptr && host_window()->titlebar_style() == TitlebarStyle::Hidden;
        impl_->title_widget->set_visible(!hide_title);
        const auto request = impl_->title_widget->measure(row_constraints);
        const float width = std::min(request.natural_width, impl_->title_bounds.width);
        const float child_height = std::min(request.natural_height, impl_->title_bounds.height);
        const float x = width <= impl_->title_bounds.width
                            ? impl_->title_bounds.x + ((impl_->title_bounds.width - width) * 0.5F)
                            : impl_->title_bounds.x;
        const float y =
            impl_->title_bounds.y + ((impl_->title_bounds.height - child_height) * 0.5F);
        impl_->title_widget->allocate({x, y, width, child_height});
    }
}

bool Headerbar::hit_test(Point point) const {
    return Widget::hit_test(point) || impl_->overflow_popover->hit_test(point);
}

std::vector<Rect> Headerbar::damage_regions() const {
    auto regions = Widget::damage_regions();
    const auto popup = impl_->overflow_popover->damage_regions();
    regions.insert(regions.end(), popup.begin(), popup.end());
    return regions;
}

bool Headerbar::handle_mouse_event(const MouseEvent& event) {
    const Point point{event.x, event.y};
    const bool on_overflow = impl_->has_trailing_overflow && impl_->overflow_rect.contains(point);
    if (event.type == MouseEvent::Type::Press && event.button == 1 && on_overflow) {
        impl_->overflow_armed = true;
        return true;
    }
    if (event.type == MouseEvent::Type::Release && event.button == 1) {
        const bool open = impl_->overflow_armed && on_overflow;
        impl_->overflow_armed = false;
        if (open) {
            open_trailing_overflow();
            return true;
        }
    }
    if (event.type != MouseEvent::Type::Press || event.button != 1 ||
        !allocation().contains(point)) {
        return false;
    }
    if (auto* window = host_window(); window != nullptr) {
        if (event.click_count >= 2) {
            window->toggle_maximize();
            return true;
        }
        return window->begin_system_move(event.native_serial);
    }
    return false;
}

void Headerbar::snapshot(SnapshotContext& ctx) const {
    if (const auto* window = host_window();
        window != nullptr && !window->uses_client_side_decorations()) {
        return;
    }

    const auto allocation = this->allocation();
    const bool focused = host_window() == nullptr || host_window()->is_focused();
    const auto background =
        focused
            ? theme_color("background", theme_color("headerbar-bg", Color::from_rgb(253, 253, 254)))
            : theme_color("headerbar-backdrop",
                          theme_color("window-bg", Color::from_rgb(248, 249, 252)));
    const auto foreground =
        theme_color("text-color", theme_color("headerbar-fg", Color::from_rgb(37, 40, 46)));
    const auto border =
        theme_color("border-color", theme_color("headerbar-shade", Color::from_rgb(224, 228, 234)));
    ctx.add_color_rect(allocation, background);

    const bool hide_title =
        host_window() != nullptr && host_window()->titlebar_style() == TitlebarStyle::Hidden;
    if (impl_->has_trailing_overflow) {
        const auto font = FontDescriptor{.family = {}, .size = 13.5F, .weight = FontWeight::Medium};
        const auto mark = measure_text("…", font);
        const auto& rect = impl_->overflow_rect;
        ctx.add_text({rect.x + ((rect.width - mark.width) * 0.5F),
                      rect.y + ((rect.height - mark.height) * 0.5F)},
                     "…",
                     foreground,
                     font);
    }

    if (impl_->title_widget != nullptr) {
        Widget::snapshot(ctx);
        ctx.add_color_rect({allocation.x, allocation.bottom() - 1.0F, allocation.width, 1.0F},
                           border);
        return;
    }
    const auto title_text = impl_->title.empty() && host_window() != nullptr
                                ? std::string(host_window()->title())
                                : impl_->title;
    if (!hide_title && !title_text.empty() && impl_->title_bounds.width > 0.0F) {
        const auto font = headerbar_title_font(theme_number("title-font-size", 14.0F));
        const auto measured = measure_text(title_text, font);
        const auto centered_x = [&](float width) {
            return width <= impl_->title_bounds.width
                       ? impl_->title_bounds.x + ((impl_->title_bounds.width - width) * 0.5F)
                       : impl_->title_bounds.x;
        };

        if (impl_->subtitle.empty()) {
            const Point origin{
                centered_x(measured.width),
                impl_->title_bounds.y + ((impl_->title_bounds.height - measured.height) * 0.5F),
            };
            add_text_elided(
                ctx, origin, title_text, measured, impl_->title_bounds.width, foreground, font);
        } else {
            // GNOME stacks a dimmed subtitle under the title, the pair centered
            // together as one heading block.
            const auto subtitle_font =
                headerbar_title_font(theme_number("subtitle-font-size", 11.0F));
            const auto subtitle_measured = measure_text(impl_->subtitle, subtitle_font);
            const float gap = 1.0F;
            const float block_height = measured.height + gap + subtitle_measured.height;
            const float top =
                impl_->title_bounds.y + ((impl_->title_bounds.height - block_height) * 0.5F);

            add_text_elided(ctx,
                            {centered_x(measured.width), top},
                            title_text,
                            measured,
                            impl_->title_bounds.width,
                            foreground,
                            font);
            add_text_elided(ctx,
                            {centered_x(subtitle_measured.width), top + measured.height + gap},
                            impl_->subtitle,
                            subtitle_measured,
                            impl_->title_bounds.width,
                            // The fallback-free overload takes a *property*
                            // name, not a token name: "secondary-text-color" is
                            // the property that resolves the text-secondary
                            // token.
                            theme_color("subtitle-color", theme_color("secondary-text-color")),
                            subtitle_font);
        }
    }

    Widget::snapshot(ctx);
    ctx.add_color_rect({allocation.x, allocation.bottom() - 1.0F, allocation.width, 1.0F}, border);
}

} // namespace nk
