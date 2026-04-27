#include <algorithm>
#include <cctype>
#include <cmath>
#include <nk/accessibility/accessible.h>
#include <nk/accessibility/role.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/command_palette.h>
#include <sstream>
#include <utility>

namespace nk {

namespace {

FontDescriptor palette_font(float size = 13.0F, FontWeight weight = FontWeight::Regular) {
    return FontDescriptor{
        .family = {},
        .size = size,
        .weight = weight,
    };
}

std::string ascii_lower(std::string_view text) {
    std::string value;
    value.reserve(text.size());
    for (const char ch : text) {
        value.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return value;
}

bool command_matches(const CommandPaletteCommand& command, std::string_view lower_query) {
    if (lower_query.empty()) {
        return true;
    }

    const auto query = std::string(lower_query);
    return ascii_lower(command.id).find(query) != std::string::npos ||
           ascii_lower(command.title).find(query) != std::string::npos ||
           ascii_lower(command.subtitle).find(query) != std::string::npos ||
           ascii_lower(command.category).find(query) != std::string::npos;
}

Rect palette_inner_rect(const CommandPalette& palette) {
    auto body = palette.allocation();
    if (has_flag(palette.state_flags(), StateFlags::Focused)) {
        body = {body.x + 2.0F, body.y + 2.0F, body.width - 4.0F, body.height - 4.0F};
    }
    return {
        body.x + 1.0F,
        body.y + 1.0F,
        std::max(0.0F, body.width - 2.0F),
        std::max(0.0F, body.height - 2.0F),
    };
}

Rect search_rect(const CommandPalette& palette) {
    const auto inner = palette_inner_rect(palette);
    return {inner.x + 10.0F, inner.y + 10.0F, std::max(0.0F, inner.width - 20.0F), 38.0F};
}

Rect results_rect(const CommandPalette& palette) {
    const auto inner = palette_inner_rect(palette);
    constexpr float SearchHeight = 58.0F;
    return {
        inner.x,
        inner.y + SearchHeight,
        inner.width,
        std::max(0.0F, inner.height - SearchHeight),
    };
}

float max_scroll_offset(std::size_t rows, float row_height, float viewport_height) {
    if (viewport_height <= 0.0F) {
        return 0.0F;
    }
    return std::max(0.0F, (static_cast<float>(rows) * row_height) - viewport_height);
}

void clamp_scroll_offset(float& offset, std::size_t rows, float row_height, float viewport_height) {
    offset = std::clamp(offset, 0.0F, max_scroll_offset(rows, row_height, viewport_height));
}

std::string palette_accessible_description(std::span<const CommandPaletteCommand> commands,
                                           std::span<const std::size_t> filtered,
                                           std::string_view query,
                                           std::optional<std::size_t> current_result) {
    std::ostringstream stream;
    stream << filtered.size() << " results";
    stream << ", " << commands.size() << " commands";
    if (!query.empty()) {
        stream << ", query " << query;
    }
    if (filtered.empty()) {
        stream << ", empty";
        return stream.str();
    }
    if (current_result && *current_result < filtered.size()) {
        const auto source = filtered[*current_result];
        const auto& command = commands[source];
        stream << ", current result " << (*current_result + 1) << " of " << filtered.size();
        stream << ", " << command.title;
        if (!command.enabled) {
            stream << ", disabled";
        }
    } else {
        stream << ", no current result";
    }
    return stream.str();
}

std::string palette_accessible_value(std::span<const CommandPaletteCommand> commands,
                                     std::span<const std::size_t> filtered,
                                     std::optional<std::size_t> current_result) {
    if (!current_result || *current_result >= filtered.size()) {
        return {};
    }

    const auto source = filtered[*current_result];
    const auto& command = commands[source];
    std::ostringstream stream;
    stream << command.title;
    if (!command.subtitle.empty()) {
        stream << ", " << command.subtitle;
    }
    if (!command.category.empty()) {
        stream << ", " << command.category;
    }
    if (!command.enabled) {
        stream << ", disabled";
    }
    return stream.str();
}

std::size_t invalid_row() {
    return static_cast<std::size_t>(-1);
}

} // namespace

struct CommandPalette::Impl {
    std::vector<CommandPaletteCommand> commands;
    std::vector<std::size_t> filtered;
    std::string query;
    std::optional<std::size_t> current_result;
    std::size_t hovered_result = invalid_row();
    float row_height = 48.0F;
    float scroll_offset = 0.0F;
    bool focused_state = false;
    Signal<std::string_view> command_activated;
    Signal<> dismiss_requested;
};

std::shared_ptr<CommandPalette> CommandPalette::create() {
    return std::shared_ptr<CommandPalette>(new CommandPalette());
}

CommandPalette::CommandPalette() : impl_(std::make_unique<Impl>()) {
    add_style_class("command-palette");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::Dialog);
    accessible.set_name("Command palette");
    accessible.add_action(AccessibleAction::Activate, [this] { return activate_current(); });
    set_focusable(true);
    rebuild_filter();
    sync_accessible_summary();
}

CommandPalette::~CommandPalette() = default;

void CommandPalette::set_commands(std::vector<CommandPaletteCommand> commands) {
    impl_->commands = std::move(commands);
    rebuild_filter();
    queue_layout();
    queue_redraw();
}

std::span<const CommandPaletteCommand> CommandPalette::commands() const {
    return impl_->commands;
}

void CommandPalette::set_query(std::string query) {
    if (impl_->query == query) {
        return;
    }
    impl_->query = std::move(query);
    rebuild_filter();
    queue_redraw();
}

std::string_view CommandPalette::query() const {
    return impl_->query;
}

void CommandPalette::clear_query() {
    set_query({});
}

std::optional<std::size_t> CommandPalette::current_command() const {
    if (!impl_->current_result || *impl_->current_result >= impl_->filtered.size()) {
        return std::nullopt;
    }
    return impl_->filtered[*impl_->current_result];
}

Signal<std::string_view>& CommandPalette::on_command_activated() {
    return impl_->command_activated;
}

Signal<>& CommandPalette::on_dismiss_requested() {
    return impl_->dismiss_requested;
}

SizeRequest CommandPalette::measure(const Constraints& /*constraints*/) const {
    const float visible_rows = static_cast<float>(
        std::min<std::size_t>(6, std::max<std::size_t>(3, impl_->filtered.size())));
    const float height = 58.0F + (visible_rows * impl_->row_height) + 2.0F;
    return {280.0F, 160.0F, 440.0F, std::max(220.0F, height)};
}

void CommandPalette::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->filtered.size(),
                        impl_->row_height,
                        results_rect(*this).height);
    ensure_current_visible();
    sync_accessible_summary();
}

bool CommandPalette::handle_mouse_event(const MouseEvent& event) {
    const Point point{event.x, event.y};
    if (event.type == MouseEvent::Type::Press && event.button == 1) {
        if (search_rect(*this).contains(point)) {
            grab_focus();
            return true;
        }
        const auto result = result_at_point(point);
        if (!result) {
            return palette_inner_rect(*this).contains(point);
        }
        set_current_result(result);
        if (event.click_count >= 2) {
            (void)activate_current();
        }
        return true;
    }

    if (event.type == MouseEvent::Type::Scroll) {
        const auto results = results_rect(*this);
        if (!results.contains(point)) {
            return false;
        }
        impl_->scroll_offset = std::clamp(
            impl_->scroll_offset - (event.scroll_dy * impl_->row_height),
            0.0F,
            max_scroll_offset(impl_->filtered.size(), impl_->row_height, results.height));
        queue_redraw();
        return true;
    }

    if (event.type == MouseEvent::Type::Move) {
        const auto result = result_at_point(point);
        const auto hovered = result.value_or(invalid_row());
        if (hovered != impl_->hovered_result) {
            impl_->hovered_result = hovered;
            queue_redraw();
        }
        return palette_inner_rect(*this).contains(point);
    }

    if (event.type == MouseEvent::Type::Leave) {
        if (impl_->hovered_result != invalid_row()) {
            impl_->hovered_result = invalid_row();
            queue_redraw();
        }
        return false;
    }

    return false;
}

bool CommandPalette::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    switch (event.key) {
    case KeyCode::Up:
        move_current(-1);
        return true;
    case KeyCode::Down:
        move_current(1);
        return true;
    case KeyCode::Home:
        set_current_result(impl_->filtered.empty() ? std::nullopt : std::optional<std::size_t>{0});
        return true;
    case KeyCode::End:
        set_current_result(impl_->filtered.empty()
                               ? std::nullopt
                               : std::optional<std::size_t>{impl_->filtered.size() - 1});
        return true;
    case KeyCode::PageUp:
        move_current(-static_cast<int>(std::max<std::size_t>(
            1, static_cast<std::size_t>(results_rect(*this).height / impl_->row_height))));
        return true;
    case KeyCode::PageDown:
        move_current(static_cast<int>(std::max<std::size_t>(
            1, static_cast<std::size_t>(results_rect(*this).height / impl_->row_height))));
        return true;
    case KeyCode::Return:
        return activate_current();
    case KeyCode::Escape:
        if (!impl_->query.empty()) {
            clear_query();
        } else {
            impl_->dismiss_requested.emit();
        }
        return true;
    case KeyCode::Backspace:
        if (!impl_->query.empty()) {
            impl_->query.pop_back();
            rebuild_filter();
            queue_redraw();
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool CommandPalette::handle_text_input_event(const TextInputEvent& event) {
    if (event.type != TextInputEvent::Type::Commit || event.text.empty()) {
        return false;
    }
    impl_->query += event.text;
    rebuild_filter();
    queue_redraw();
    return true;
}

CursorShape CommandPalette::cursor_shape() const {
    return CursorShape::IBeam;
}

void CommandPalette::on_focus_changed(bool focused) {
    impl_->focused_state = focused;
    queue_redraw();
}

void CommandPalette::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 10.0F);
    auto body = a;
    if (has_flag(state_flags(), StateFlags::Focused)) {
        const auto focus_ring = theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F});
        ctx.add_rounded_rect(
            a, Color{focus_ring.r, focus_ring.g, focus_ring.b, 0.08F}, corner_radius + 1.0F);
        body = {a.x + 1.5F, a.y + 1.5F, a.width - 3.0F, a.height - 3.0F};
    }

    const auto background = theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F});
    const auto border = theme_color("border-color", Color{0.82F, 0.84F, 0.88F, 1.0F});
    const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    const auto muted_text = theme_color("muted-text-color", Color{0.42F, 0.45F, 0.50F, 1.0F});
    const auto selected_bg = theme_color("selected-background", Color{0.86F, 0.92F, 0.99F, 1.0F});
    const auto focus_ring = theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F});
    const auto hover_bg = theme_color("hover-background", Color{0.94F, 0.95F, 0.97F, 1.0F});
    const auto disabled_text = theme_color("disabled-text-color", Color{0.58F, 0.60F, 0.65F, 1.0F});
    const auto row_separator = theme_color("row-separator-color", Color{0.90F, 0.91F, 0.94F, 1.0F});

    ctx.add_rounded_rect(body, background, corner_radius);
    ctx.add_border(body, border, 1.0F, corner_radius);

    const auto search = search_rect(*this);
    ctx.add_rounded_rect(
        search, theme_color("search-background", Color{0.96F, 0.97F, 0.98F, 1.0F}), 8.0F);
    ctx.add_border(
        search, theme_color("search-border-color", Color{0.84F, 0.86F, 0.90F, 1.0F}), 1.0F, 8.0F);

    const auto search_font = palette_font(13.5F);
    const auto query_text = impl_->query.empty() ? std::string("Search commands...") : impl_->query;
    const auto query_color = impl_->query.empty() ? muted_text : text_color;
    const auto query_measured = measure_text(query_text, search_font);
    const float query_y = search.y + std::max(0.0F, (search.height - query_measured.height) * 0.5F);
    ctx.push_rounded_clip(
        {search.x + 12.0F, search.y, std::max(0.0F, search.width - 24.0F), search.height}, 6.0F);
    ctx.add_text({search.x + 12.0F, query_y}, query_text, query_color, search_font);
    if (has_flag(state_flags(), StateFlags::Focused)) {
        const auto before_cursor = measure_text(impl_->query, search_font);
        const float caret_x = search.x + 12.0F + before_cursor.width;
        ctx.add_color_rect({caret_x, search.y + 8.0F, 1.5F, std::max(0.0F, search.height - 16.0F)},
                           theme_color("caret-color", text_color));
    }
    ctx.pop_container();

    const auto results = results_rect(*this);
    ctx.push_rounded_clip(results, corner_radius);
    if (impl_->filtered.empty()) {
        const std::string empty_text =
            impl_->query.empty() ? "No commands" : "No matching commands";
        const auto measured = measure_text(empty_text, search_font);
        ctx.add_text({results.x + 14.0F,
                      results.y + std::max(0.0F, (results.height - measured.height) * 0.5F)},
                     empty_text,
                     muted_text,
                     search_font);
        ctx.pop_container();
        return;
    }

    const auto title_font = palette_font(13.0F, FontWeight::Medium);
    const auto detail_font = palette_font(12.0F);
    const auto first_result = static_cast<std::size_t>(impl_->scroll_offset / impl_->row_height);
    float y = results.y - std::fmod(impl_->scroll_offset, impl_->row_height);
    for (std::size_t result = first_result; result < impl_->filtered.size() && y < results.bottom();
         ++result) {
        const auto source = impl_->filtered[result];
        const auto& command = impl_->commands[source];
        const Rect row_rect = {results.x + 6.0F,
                               y + 2.0F,
                               std::max(0.0F, results.width - 12.0F),
                               impl_->row_height - 4.0F};
        const bool current = impl_->current_result && *impl_->current_result == result;
        const bool hovered = impl_->hovered_result == result;
        if (current) {
            ctx.add_rounded_rect(
                row_rect, Color{selected_bg.r, selected_bg.g, selected_bg.b, 0.82F}, 7.0F);
            ctx.add_border(
                row_rect, Color{focus_ring.r, focus_ring.g, focus_ring.b, 0.75F}, 1.0F, 7.0F);
        } else if (hovered) {
            ctx.add_rounded_rect(row_rect, hover_bg, 7.0F);
        }

        const auto command_text = command.enabled ? text_color : disabled_text;
        const auto detail_color = command.enabled ? muted_text : disabled_text;
        ctx.push_rounded_clip({row_rect.x + 10.0F,
                               row_rect.y,
                               std::max(0.0F, row_rect.width - 20.0F),
                               row_rect.height},
                              5.0F);
        ctx.add_text(
            {row_rect.x + 10.0F, row_rect.y + 7.0F}, command.title, command_text, title_font);
        std::string detail;
        if (!command.category.empty()) {
            detail += command.category;
        }
        if (!command.subtitle.empty()) {
            if (!detail.empty()) {
                detail += " - ";
            }
            detail += command.subtitle;
        }
        if (!command.enabled) {
            if (!detail.empty()) {
                detail += " - ";
            }
            detail += "Disabled";
        }
        if (!detail.empty()) {
            ctx.add_text(
                {row_rect.x + 10.0F, row_rect.y + 26.0F}, detail, detail_color, detail_font);
        }
        ctx.pop_container();
        ctx.add_color_rect(
            {results.x + 10.0F, y + impl_->row_height - 1.0F, results.width - 20.0F, 1.0F},
            row_separator);
        y += impl_->row_height;
    }
    ctx.pop_container();
}

void CommandPalette::rebuild_filter() {
    impl_->filtered.clear();
    const auto lower_query = ascii_lower(impl_->query);
    for (std::size_t index = 0; index < impl_->commands.size(); ++index) {
        if (command_matches(impl_->commands[index], lower_query)) {
            impl_->filtered.push_back(index);
        }
    }
    if (impl_->filtered.empty()) {
        impl_->current_result = std::nullopt;
        impl_->hovered_result = invalid_row();
        impl_->scroll_offset = 0.0F;
    } else if (!impl_->current_result || *impl_->current_result >= impl_->filtered.size()) {
        impl_->current_result = 0;
    }
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->filtered.size(),
                        impl_->row_height,
                        results_rect(*this).height);
    ensure_current_visible();
    sync_accessible_summary();
}

void CommandPalette::sync_accessible_summary() {
    auto& accessible = ensure_accessible();
    accessible.set_description(palette_accessible_description(
        impl_->commands, impl_->filtered, impl_->query, impl_->current_result));
    accessible.set_value(
        palette_accessible_value(impl_->commands, impl_->filtered, impl_->current_result));
}

void CommandPalette::move_current(int delta) {
    if (impl_->filtered.empty()) {
        set_current_result(std::nullopt);
        return;
    }
    const int current = impl_->current_result ? static_cast<int>(*impl_->current_result) : 0;
    const int max_index = static_cast<int>(impl_->filtered.size() - 1);
    set_current_result(static_cast<std::size_t>(std::clamp(current + delta, 0, max_index)));
}

void CommandPalette::set_current_result(std::optional<std::size_t> result) {
    if (result && *result >= impl_->filtered.size()) {
        result = std::nullopt;
    }
    if (impl_->current_result == result) {
        return;
    }
    impl_->current_result = result;
    ensure_current_visible();
    sync_accessible_summary();
    queue_redraw();
}

void CommandPalette::ensure_current_visible() {
    if (!impl_->current_result) {
        return;
    }
    const auto results = results_rect(*this);
    const float row_top = static_cast<float>(*impl_->current_result) * impl_->row_height;
    const float row_bottom = row_top + impl_->row_height;
    if (row_top < impl_->scroll_offset) {
        impl_->scroll_offset = row_top;
    } else if (row_bottom > impl_->scroll_offset + results.height) {
        impl_->scroll_offset = row_bottom - results.height;
    }
    clamp_scroll_offset(
        impl_->scroll_offset, impl_->filtered.size(), impl_->row_height, results.height);
}

bool CommandPalette::activate_current() {
    const auto current = current_command();
    if (!current) {
        return false;
    }
    const auto& command = impl_->commands[*current];
    if (!command.enabled) {
        return false;
    }
    impl_->command_activated.emit(command.id);
    return true;
}

std::optional<std::size_t> CommandPalette::result_at_point(Point point) const {
    const auto results = results_rect(*this);
    if (!results.contains(point) || impl_->filtered.empty()) {
        return std::nullopt;
    }
    const float local_y = point.y - results.y + impl_->scroll_offset;
    const auto result = static_cast<std::size_t>(local_y / impl_->row_height);
    if (result >= impl_->filtered.size()) {
        return std::nullopt;
    }
    return result;
}

} // namespace nk
