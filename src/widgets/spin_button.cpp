#include <algorithm>
#include <cmath>
#include <locale>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/spin_button.h>
#include <sstream>
#include <utility>

namespace nk {

namespace {

FontDescriptor spin_button_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Medium,
    };
}

std::string format_value(double value, int precision) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss.precision(precision);
    oss << std::fixed << value;
    return oss.str();
}

std::optional<double> parse_value(std::string_view text, int precision) {
    const auto dot = text.find('.');
    if (text.empty() || text.find_first_not_of("-0123456789.") != std::string_view::npos ||
        (dot != std::string_view::npos &&
         (precision == 0 || text.size() - dot - 1 > static_cast<std::size_t>(precision)))) {
        return std::nullopt;
    }
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    double value = 0.0;
    if (!(input >> value) || !input.eof() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

} // namespace

struct SpinButton::Impl {
    double value = 0.0;
    double min_val = 0.0;
    double max_val = 100.0;
    double step = 1.0;
    int precision = 0;
    Signal<double> value_changed;
    ScopedConnection edited;
    double initial_value = 0.0;
    bool formatting = false;
    bool first_click = false;
    int armed_button = 0; // -1 = dec, 0 = none, 1 = inc
};

std::shared_ptr<SpinButton> SpinButton::create() {
    return std::shared_ptr<SpinButton>(new SpinButton());
}

SpinButton::SpinButton() : TextField("0"), impl_(std::make_unique<Impl>()) {
    remove_style_class("text-field");
    add_style_class("spin-button");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::SpinButton);
    accessible.set_name({});
    impl_->edited = ScopedConnection(on_text_changed().connect([this](std::string_view draft) {
        if (impl_->formatting) {
            return;
        }
        const auto number = parse_value(draft, impl_->precision);
        // An incomplete or out-of-range edit must never leave a valid prefix
        // (for example the first six digits of a large quantity) as the value.
        const double next =
            number && *number >= minimum() && *number <= maximum() ? *number : impl_->initial_value;
        if (next != value()) {
            impl_->value = next;
            impl_->value_changed.emit(next);
        }
    }));
    accessible.add_action(AccessibleAction::Increment, [this] {
        set_value(value() + step());
        return true;
    });
    accessible.add_action(AccessibleAction::Decrement, [this] {
        set_value(value() - step());
        return true;
    });
    accessible.set_value(format_value(impl_->value, impl_->precision));
    sync_accessible_range();
    accessible.set_numeric_setter([this](double value) {
        const double before = impl_->value;
        set_value(value);
        return impl_->value != before;
    });
    accessible.add_action(AccessibleAction::Focus, [this]() {
        grab_focus();
        return true;
    });
}

SpinButton::~SpinButton() = default;

double SpinButton::value() const {
    return impl_->value;
}

void SpinButton::set_value(double val) {
    val = std::clamp(val, impl_->min_val, impl_->max_val);
    const bool changed = impl_->value != val;
    impl_->value = val;
    impl_->initial_value = val;
    impl_->formatting = true;
    set_text(format_value(val, impl_->precision));
    impl_->formatting = false;
    if (changed) {
        impl_->value_changed.emit(val);
    }
}

double SpinButton::minimum() const {
    return impl_->min_val;
}

double SpinButton::maximum() const {
    return impl_->max_val;
}

void SpinButton::set_range(double min, double max) {
    impl_->min_val = min;
    impl_->max_val = max;
    set_value(impl_->value);
    sync_accessible_range();
    queue_layout();
}

double SpinButton::step() const {
    return impl_->step;
}

void SpinButton::set_step(double s) {
    impl_->step = s;
    sync_accessible_range();
}

void SpinButton::sync_accessible_range() {
    ensure_accessible().set_numeric_range({impl_->min_val, impl_->max_val, impl_->step});
}

int SpinButton::precision() const {
    return impl_->precision;
}

void SpinButton::set_precision(int digits) {
    impl_->precision = std::max(0, digits);
    set_value(value());
    queue_layout();
    queue_redraw();
}

Signal<double>& SpinButton::on_value_changed() {
    return impl_->value_changed;
}

SizeRequest SpinButton::measure(const Constraints& /*constraints*/) const {
    const float digits =
        std::max(measure_text(format_value(minimum(), precision()), spin_button_font()).width,
                 measure_text(format_value(maximum(), precision()), spin_button_font()).width);
    const float w = std::max(theme_number("min-width", 120.0F),
                             2 * theme_number("button-width", 28.0F) + digits + 16.0F);
    const float h = theme_number("min-height", 36.0F);
    return {w, h, w, h};
}

bool SpinButton::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return TextField::handle_mouse_event(event);
    }

    const auto a = allocation();
    const float button_width = std::min(a.width / 3.0F, theme_number("button-width", 28.0F));
    const auto point = Point{event.x, event.y};

    auto zone_at = [&](Point pt) -> int {
        if (!a.contains(pt)) {
            return 0;
        }
        if (pt.x < a.x + button_width) {
            return -1; // decrement
        }
        if (pt.x > a.right() - button_width) {
            return 1; // increment
        }
        return 0; // middle
    };

    switch (event.type) {
    case MouseEvent::Type::Press: {
        const int zone = zone_at(point);
        if (!a.contains(point)) {
            return false;
        }
        impl_->armed_button = zone;
        const bool keep_selection = std::exchange(impl_->first_click, false);
        if (zone == 0 && !keep_selection && has_flag(state_flags(), StateFlags::Focused)) {
            return TextField::handle_mouse_event(event);
        }
        grab_focus();
        queue_redraw();
        return true;
    }
    case MouseEvent::Type::Release: {
        const int zone = zone_at(point);
        const bool activate = impl_->armed_button != 0 && zone == impl_->armed_button;
        impl_->armed_button = 0;
        queue_redraw();
        if (activate) {
            if (zone == -1) {
                set_value(impl_->value - impl_->step);
            } else if (zone == 1) {
                set_value(impl_->value + impl_->step);
            }
            return true;
        }
        return TextField::handle_mouse_event(event) || a.contains(point);
    }
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
    case MouseEvent::Type::DragStart:
    case MouseEvent::Type::DragUpdate:
    case MouseEvent::Type::DragEnd:
        return TextField::handle_mouse_event(event);
    }

    return false;
}

bool SpinButton::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    // Keep navigation shortcuts when selecting a value. A caret or partial
    // selection uses the normal text-editing keys.
    const bool navigation = event.key == KeyCode::Home || event.key == KeyCode::End ||
                            event.key == KeyCode::Left || event.key == KeyCode::Right;
    const bool whole_value =
        has_selection() && selection_end() - selection_start() == text().size();
    if (navigation && (event.modifiers != Modifiers::None ||
                       (has_flag(state_flags(), StateFlags::Focused) && !whole_value))) {
        return TextField::handle_key_event(event);
    }
    switch (event.key) {
    case KeyCode::Home:
        set_value(minimum());
        break;
    case KeyCode::End:
        set_value(maximum());
        break;
    case KeyCode::Right:
    case KeyCode::Up:
        set_value(value() + step());
        break;
    case KeyCode::Left:
    case KeyCode::Down:
        set_value(value() - step());
        break;
    case KeyCode::Escape:
        if (has_preedit()) {
            return TextField::handle_key_event(event);
        }
        set_value(impl_->initial_value);
        break;
    case KeyCode::Return:
        if (!has_preedit()) {
            set_value(value());
            select_all();
        }
        return TextField::handle_key_event(event);
    default:
        return TextField::handle_key_event(event);
    }
    select_all();
    return true;
}

void SpinButton::on_focus_changed(bool focused) {
    TextField::on_focus_changed(focused);
    if (focused) {
        impl_->initial_value = value();
        impl_->first_click = has_flag(state_flags(), StateFlags::Pressed);
        select_all();
    } else {
        set_value(value());
    }
}

Rect SpinButton::text_rect() const {
    const auto body = inner_body_rect();
    const float side =
        std::min(allocation().width / 3.0F, theme_number("button-width", 28.0F)) + 4.0F;
    return {body.x + side, body.y, std::max(0.0F, body.width - 2 * side), body.height};
}

CursorShape SpinButton::cursor_shape() const {
    return CursorShape::PointingHand;
}

void SpinButton::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 10.0F);
    const float button_width = std::min(a.width / 3.0F, theme_number("button-width", 28.0F));
    auto body = a;

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a, theme_color("focus-ring-color"), corner_radius + 2.0F);
        body = {a.x + 2.0F, a.y + 2.0F, a.width - 4.0F, a.height - 4.0F};
    }

    // Background
    ctx.add_rounded_rect(
        body, theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F}), 1.0F, corner_radius);

    const auto font = spin_button_font();
    const auto border_color = theme_color("border-color", Color{0.78F, 0.8F, 0.84F, 1.0F});
    const auto button_bg = theme_color("button-background", Color{0.94F, 0.95F, 0.97F, 1.0F});
    const auto armed_bg = theme_color("armed-background", Color{0.88F, 0.9F, 0.93F, 1.0F});

    const auto button_text_color =
        theme_color("button-text-color", Color{0.25F, 0.27F, 0.3F, 1.0F});

    // Left divider
    ctx.add_color_rect({body.x + button_width, body.y + 1.0F, 1.0F, body.height - 2.0F},
                       border_color);
    // Right divider
    ctx.add_color_rect({body.right() - button_width, body.y + 1.0F, 1.0F, body.height - 2.0F},
                       border_color);

    // Decrement button background
    {
        const auto bg = impl_->armed_button == -1 ? armed_bg : button_bg;
        ctx.add_color_rect({body.x + 1.0F, body.y + 1.0F, button_width - 1.0F, body.height - 2.0F},
                           bg);
    }

    // Increment button background
    {
        const auto bg = impl_->armed_button == 1 ? armed_bg : button_bg;
        ctx.add_color_rect({body.right() - button_width + 1.0F,
                            body.y + 1.0F,
                            button_width - 2.0F,
                            body.height - 2.0F},
                           bg);
    }

    // Decrement "-" text
    {
        const auto measured = measure_text("-", font);
        const float text_x = body.x + (button_width - measured.width) * 0.5F;
        const float text_y = body.y + std::max(0.0F, (body.height - measured.height) * 0.5F);
        ctx.add_text({text_x, text_y}, "-", button_text_color, font);
    }

    // Increment "+" text
    {
        const auto measured = measure_text("+", font);
        const float text_x = body.right() - button_width + (button_width - measured.width) * 0.5F;
        const float text_y = body.y + std::max(0.0F, (body.height - measured.height) * 0.5F);
        ctx.add_text({text_x, text_y}, "+", button_text_color, font);
    }

    snapshot_text(ctx);
}

} // namespace nk
