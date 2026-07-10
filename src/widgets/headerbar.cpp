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

} // namespace

struct Headerbar::Impl {
    std::string title;
    std::string decoration_layout;
    HeaderbarCenteringPolicy centering_policy = HeaderbarCenteringPolicy::Strict;
    std::vector<std::shared_ptr<Widget>> leading;
    std::vector<std::shared_ptr<Widget>> trailing;
    std::shared_ptr<HeaderbarControl> back;
    std::shared_ptr<HeaderbarControl> minimize;
    std::shared_ptr<HeaderbarControl> maximize;
    std::shared_ptr<HeaderbarControl> close;
    ScopedConnection back_connection;
    ScopedConnection minimize_connection;
    ScopedConnection maximize_connection;
    ScopedConnection close_connection;
    Signal<> back_requested;
    Rect title_bounds{};
    bool window_controls_enabled = true;
    bool show_back_button = false;
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
    const auto title_text = impl_->title.empty() && host_window() != nullptr
                                ? std::string(host_window()->title())
                                : impl_->title;
    if (!title_text.empty()) {
        natural_width +=
            measure_text(title_text, headerbar_title_font(theme_number("title-font-size", 14.0F)))
                .width +
            spacing;
    }
    return {0.0F, height, natural_width, height};
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
    for (auto iterator = impl_->trailing.rbegin(); iterator != impl_->trailing.rend(); ++iterator) {
        const auto request = (*iterator)->measure(row_constraints);
        const float width =
            std::min(request.natural_width, std::max(0.0F, trailing_x - allocation.x));
        const float child_height = std::min(request.natural_height, height);
        trailing_x -= width;
        const float y = allocation.y + ((height - child_height) * 0.5F);
        (*iterator)->allocate({trailing_x, y, width, child_height});
        trailing_x -= spacing;
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
        const auto title_text = impl_->title.empty() && host_window() != nullptr
                                    ? std::string(host_window()->title())
                                    : impl_->title;
        const float title_width =
            measure_text(title_text, headerbar_title_font(theme_number("title-font-size", 14.0F)))
                .width;
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
}

bool Headerbar::handle_mouse_event(const MouseEvent& event) {
    if (event.type != MouseEvent::Type::Press || event.button != 1 ||
        !allocation().contains({event.x, event.y})) {
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
    const auto title_text = impl_->title.empty() && host_window() != nullptr
                                ? std::string(host_window()->title())
                                : impl_->title;
    if (!hide_title && !title_text.empty() && impl_->title_bounds.width > 0.0F) {
        const auto font = headerbar_title_font(theme_number("title-font-size", 14.0F));
        const auto measured = measure_text(title_text, font);
        const float text_x =
            measured.width <= impl_->title_bounds.width
                ? impl_->title_bounds.x + ((impl_->title_bounds.width - measured.width) * 0.5F)
                : impl_->title_bounds.x;
        const Point origin{
            text_x,
            impl_->title_bounds.y + ((impl_->title_bounds.height - measured.height) * 0.5F),
        };
        add_text_elided(
            ctx, origin, title_text, measured, impl_->title_bounds.width, foreground, font);
    }

    Widget::snapshot(ctx);
    ctx.add_color_rect({allocation.x, allocation.bottom() - 1.0F, allocation.width, 1.0F}, border);
}

} // namespace nk
