#include <algorithm>
#include <nk/accessibility/accessible.h>
#include <nk/foundation/signal.h>
#include <nk/layout/constraints.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
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

constexpr float kHeaderbarHeight = 46.0F;
constexpr float kWindowControlSize = 46.0F;
constexpr float kHorizontalPadding = 12.0F;
constexpr float kItemSpacing = 6.0F;

FontDescriptor headerbar_title_font() {
    return FontDescriptor{
        .family = {},
        .size = 14.0F,
        .weight = FontWeight::Bold,
    };
}

FontDescriptor headerbar_control_font() {
    return FontDescriptor{
        .family = {},
        .size = 16.0F,
        .weight = FontWeight::Regular,
    };
}

class HeaderbarControl final : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<HeaderbarControl>
    create(std::string glyph, std::string accessible_name, bool destructive = false) {
        return std::shared_ptr<HeaderbarControl>(
            new HeaderbarControl(std::move(glyph), std::move(accessible_name), destructive));
    }

    void set_presentation(std::string glyph, std::string accessible_name) {
        if (glyph_ == glyph && accessible() != nullptr && accessible()->name() == accessible_name) {
            return;
        }
        glyph_ = std::move(glyph);
        ensure_accessible().set_name(std::move(accessible_name));
        queue_redraw();
    }

    Signal<>& on_clicked() { return clicked_; }

    [[nodiscard]] SizeRequest measure(const Constraints& /*constraints*/) const override {
        return {kWindowControlSize, kWindowControlSize, kWindowControlSize, kWindowControlSize};
    }

    bool handle_mouse_event(const MouseEvent& event) override {
        if (event.button != 1) {
            return false;
        }
        if (event.type == MouseEvent::Type::Press) {
            armed_ = allocation().contains({event.x, event.y});
            return armed_;
        }
        if (event.type == MouseEvent::Type::Release) {
            const bool activate = armed_ && allocation().contains({event.x, event.y});
            armed_ = false;
            if (activate) {
                clicked_.emit();
            }
            return activate;
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

protected:
    void snapshot(SnapshotContext& ctx) const override {
        const auto rect = allocation();
        const bool active = has_flag(state_flags(), StateFlags::Hovered) ||
                            has_flag(state_flags(), StateFlags::Pressed);
        if (active) {
            const auto fill = destructive_
                                  ? theme_color("destructive", Color::from_rgb(230, 45, 66))
                                  : theme_color("surface-hover", Color::from_rgb(240, 243, 247));
            ctx.add_rounded_rect(rect, fill, rect.height * 0.5F);
        }
        if (has_flag(state_flags(), StateFlags::Focused)) {
            ctx.add_border(rect,
                           theme_color("focus-ring-color", Color::from_rgb(53, 132, 228)),
                           2.0F,
                           rect.height * 0.5F);
        }

        const auto font = headerbar_control_font();
        const auto measured = measure_text(glyph_, font);
        const Point origin{
            rect.x + ((rect.width - measured.width) * 0.5F),
            rect.y + ((rect.height - measured.height) * 0.5F),
        };
        auto foreground =
            theme_color("headerbar-fg", theme_color("text-primary", Color::from_rgb(37, 40, 46)));
        if (destructive_ && active) {
            foreground = Color::from_rgb(255, 255, 255);
        }
        ctx.add_text(origin, glyph_, foreground, font);
    }

private:
    HeaderbarControl(std::string glyph, std::string accessible_name, bool destructive)
        : glyph_(std::move(glyph)), destructive_(destructive) {
        set_focusable(true);
        add_style_class("headerbar-control");
        if (destructive_) {
            add_style_class("destructive");
        }
        auto& accessible = ensure_accessible();
        accessible.set_role(AccessibleRole::Button);
        accessible.set_name(std::move(accessible_name));
        accessible.add_action(AccessibleAction::Activate, [this] {
            clicked_.emit();
            return true;
        });
    }

    std::string glyph_;
    bool destructive_ = false;
    bool armed_ = false;
    Signal<> clicked_;
};

} // namespace

struct Headerbar::Impl {
    std::string title;
    std::vector<std::shared_ptr<Widget>> leading;
    std::vector<std::shared_ptr<Widget>> trailing;
    std::shared_ptr<HeaderbarControl> minimize;
    std::shared_ptr<HeaderbarControl> maximize;
    std::shared_ptr<HeaderbarControl> close;
    ScopedConnection minimize_connection;
    ScopedConnection maximize_connection;
    ScopedConnection close_connection;
    Rect title_bounds{};
    bool window_controls_enabled = true;
};

std::shared_ptr<Headerbar> Headerbar::create(std::string title) {
    return std::shared_ptr<Headerbar>(new Headerbar(std::move(title)));
}

Headerbar::Headerbar(std::string title) : impl_(std::make_unique<Impl>()) {
    impl_->title = std::move(title);
    impl_->minimize = HeaderbarControl::create("−", "Minimize");
    impl_->maximize = HeaderbarControl::create("□", "Maximize");
    impl_->close = HeaderbarControl::create("×", "Close", true);

    append_child(impl_->minimize);
    append_child(impl_->maximize);
    append_child(impl_->close);

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
    accessible.set_role(AccessibleRole::Toolbar);
    accessible.set_name(impl_->title.empty() ? "Window headerbar" : impl_->title);
}

Headerbar::~Headerbar() = default;

void Headerbar::set_title(std::string title) {
    if (impl_->title == title) {
        return;
    }
    impl_->title = std::move(title);
    ensure_accessible().set_name(impl_->title.empty() ? "Window headerbar" : impl_->title);
    queue_layout();
}

void Headerbar::add_leading(std::shared_ptr<Widget> item) {
    if (item == nullptr) {
        return;
    }
    impl_->leading.push_back(item);
    insert_child(impl_->leading.size() - 1, std::move(item));
}

void Headerbar::add_trailing(std::shared_ptr<Widget> item) {
    if (item == nullptr) {
        return;
    }
    impl_->trailing.push_back(item);
    insert_child(impl_->leading.size() + impl_->trailing.size() - 1, std::move(item));
}

void Headerbar::set_window_controls_enabled(bool enabled) {
    if (impl_->window_controls_enabled == enabled) {
        return;
    }
    impl_->window_controls_enabled = enabled;
    queue_layout();
}

SizeRequest Headerbar::measure(const Constraints& constraints) const {
    if (const auto* window = host_window();
        window != nullptr && !window->uses_client_side_decorations()) {
        return {};
    }

    float natural_width = kHorizontalPadding * 2.0F;
    for (const auto& child : impl_->leading) {
        natural_width += child->measure(constraints).natural_width + kItemSpacing;
    }
    for (const auto& child : impl_->trailing) {
        natural_width += child->measure(constraints).natural_width + kItemSpacing;
    }
    if (impl_->window_controls_enabled) {
        natural_width += kWindowControlSize * 3.0F;
    }
    const auto title = impl_->title.empty() && host_window() != nullptr
                           ? std::string(host_window()->title())
                           : impl_->title;
    if (!title.empty()) {
        natural_width += measure_text(title, headerbar_title_font()).width + kItemSpacing;
    }
    return {0.0F, kHeaderbarHeight, natural_width, kHeaderbarHeight};
}

void Headerbar::allocate(const Rect& allocation) {
    Widget::allocate(allocation);

    const bool active = host_window() == nullptr || host_window()->uses_client_side_decorations();
    const bool show_controls = active && impl_->window_controls_enabled;
    for (const auto& control : {impl_->minimize, impl_->maximize, impl_->close}) {
        if (control->is_visible() != show_controls) {
            control->set_visible(show_controls);
        }
    }
    if (!active) {
        impl_->title_bounds = {};
        return;
    }

    if (auto* window = host_window(); window != nullptr) {
        impl_->maximize->set_presentation(window->is_maximized() ? "▣" : "□",
                                          window->is_maximized() ? "Restore" : "Maximize");
    }

    const Constraints row_constraints{0.0F, 0.0F, allocation.width, kHeaderbarHeight};
    float leading_x = allocation.x + kHorizontalPadding;
    for (const auto& child : impl_->leading) {
        const auto request = child->measure(row_constraints);
        const float width =
            std::min(request.natural_width, std::max(0.0F, allocation.right() - leading_x));
        const float height = std::min(request.natural_height, kHeaderbarHeight);
        const float y = allocation.y + ((kHeaderbarHeight - height) * 0.5F);
        child->allocate({leading_x, y, width, height});
        leading_x += width + kItemSpacing;
    }

    float trailing_x = allocation.right() - kHorizontalPadding;
    if (show_controls) {
        for (const auto& control : {impl_->close, impl_->maximize, impl_->minimize}) {
            trailing_x -= kWindowControlSize;
            control->allocate({trailing_x, allocation.y, kWindowControlSize, kWindowControlSize});
        }
    }
    trailing_x -= show_controls ? kItemSpacing : 0.0F;
    for (auto iterator = impl_->trailing.rbegin(); iterator != impl_->trailing.rend(); ++iterator) {
        const auto request = (*iterator)->measure(row_constraints);
        const float width =
            std::min(request.natural_width, std::max(0.0F, trailing_x - allocation.x));
        const float height = std::min(request.natural_height, kHeaderbarHeight);
        trailing_x -= width;
        const float y = allocation.y + ((kHeaderbarHeight - height) * 0.5F);
        (*iterator)->allocate({trailing_x, y, width, height});
        trailing_x -= kItemSpacing;
    }

    impl_->title_bounds = {
        leading_x,
        allocation.y,
        std::max(0.0F, trailing_x - leading_x),
        kHeaderbarHeight,
    };
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
    const auto background =
        theme_color("headerbar-bg", theme_color("window-bg", Color::from_rgb(248, 249, 252)));
    const auto foreground =
        theme_color("headerbar-fg", theme_color("text-primary", Color::from_rgb(37, 40, 46)));
    const auto border = theme_color("headerbar-border",
                                    theme_color("border-subtle", Color::from_rgb(224, 228, 234)));
    ctx.add_color_rect(allocation, background);

    const bool hide_title =
        host_window() != nullptr && host_window()->titlebar_style() == TitlebarStyle::Hidden;
    const auto title = impl_->title.empty() && host_window() != nullptr
                           ? std::string(host_window()->title())
                           : impl_->title;
    if (!hide_title && !title.empty() && impl_->title_bounds.width > 0.0F) {
        const auto font = headerbar_title_font();
        const auto measured = measure_text(title, font);
        const Point origin{
            impl_->title_bounds.x,
            impl_->title_bounds.y + ((impl_->title_bounds.height - measured.height) * 0.5F),
        };
        add_text_elided(ctx, origin, title, measured, impl_->title_bounds.width, foreground, font);
    }

    Widget::snapshot(ctx);
    ctx.add_color_rect({allocation.x, allocation.bottom() - 1.0F, allocation.width, 1.0F}, border);
}

} // namespace nk
