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
    const auto body = palette.allocation();
    return {
        body.x + 1.0F,
        body.y + 1.0F,
        std::max(0.0F, body.width - 2.0F),
        std::max(0.0F, body.height - 2.0F),
    };
}

Rect search_rect(const CommandPalette& palette, float search_height, float search_section_height) {
    const auto inner = palette_inner_rect(palette);
    return {inner.x + 10.0F,
            inner.y + std::max(0.0F, (search_section_height - search_height) * 0.5F),
            std::max(0.0F, inner.width - 20.0F),
            search_height};
}

Rect results_rect(const CommandPalette& palette, float search_section_height) {
    const auto inner = palette_inner_rect(palette);
    return {
        inner.x,
        inner.y + search_section_height,
        inner.width,
        std::max(0.0F, inner.height - search_section_height),
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
    const float height =
        resolved_search_section_height() + (visible_rows * resolved_row_height()) + 2.0F;
    return {280.0F, 160.0F, 440.0F, std::max(220.0F, height)};
}

void CommandPalette::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    const float row_height = resolved_row_height();
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->filtered.size(),
                        row_height,
                        results_rect(*this, resolved_search_section_height()).height);
    ensure_current_visible();
    sync_accessible_summary();
}

bool CommandPalette::handle_mouse_event(const MouseEvent& event) {
    const Point point{event.x, event.y};
    if (event.type == MouseEvent::Type::Press && event.button == 1) {
        if (search_rect(*this, resolved_search_height(), resolved_search_section_height())
                .contains(point)) {
            grab_focus();
            return true;
        }
        const auto result = result_at_point(point);
        if (!result) {
            return palette_inner_rect(*this).contains(point);
        }
        const auto source = impl_->filtered.at(*result);
        if (!impl_->commands.at(source).enabled) {
            return true;
        }
        set_current_result(result);
        if (event.click_count >= 2) {
            (void)activate_current();
        }
        return true;
    }

    if (event.type == MouseEvent::Type::Scroll) {
        const auto results = results_rect(*this, resolved_search_section_height());
        if (!results.contains(point)) {
            return false;
        }
        const float row_height = resolved_row_height();
        impl_->scroll_offset =
            std::clamp(impl_->scroll_offset - (event.scroll_dy * row_height),
                       0.0F,
                       max_scroll_offset(impl_->filtered.size(), row_height, results.height));
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
        for (std::size_t result = 0; result < impl_->filtered.size(); ++result) {
            if (impl_->commands.at(impl_->filtered.at(result)).enabled) {
                set_current_result(result);
                break;
            }
        }
        return true;
    case KeyCode::End:
        for (std::size_t result = impl_->filtered.size(); result > 0; --result) {
            if (impl_->commands.at(impl_->filtered.at(result - 1)).enabled) {
                set_current_result(result - 1);
                break;
            }
        }
        return true;
    case KeyCode::PageUp:
        move_current(-static_cast<int>(std::max<std::size_t>(
            1,
            static_cast<std::size_t>(results_rect(*this, resolved_search_section_height()).height /
                                     resolved_row_height()))));
        return true;
    case KeyCode::PageDown:
        move_current(static_cast<int>(std::max<std::size_t>(
            1,
            static_cast<std::size_t>(results_rect(*this, resolved_search_section_height()).height /
                                     resolved_row_height()))));
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
    const auto body = a;
    const bool focus_visible = has_flag(state_flags(), StateFlags::FocusVisible);

    const auto background = theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F});
    const auto border = theme_color("border-color");
    const auto text_color = theme_color("text-color");
    const auto muted_text = theme_color("muted-text-color");
    const auto selected_bg = theme_color("selected-background");
    const auto focus_ring = theme_color("focus-ring-color");
    const auto hover_bg = theme_color("hover-background");
    const auto disabled_text = theme_color("disabled-text-color");
    const auto row_separator = theme_color("row-separator-color");

    ctx.add_rounded_rect(body, background, corner_radius);
    ctx.add_border(body, border, 1.0F, corner_radius);
    if (focus_visible) {
        ctx.add_border({body.x + 1.5F,
                        body.y + 1.5F,
                        std::max(0.0F, body.width - 3.0F),
                        std::max(0.0F, body.height - 3.0F)},
                       focus_ring,
                       1.5F,
                       std::max(0.0F, corner_radius - 1.5F));
    }

    const float text_scale = theme_number("text-scale", 1.0F);
    const auto search =
        search_rect(*this, resolved_search_height(), resolved_search_section_height());
    ctx.add_rounded_rect(
        search, theme_color("search-background", Color{0.96F, 0.97F, 0.98F, 1.0F}), 8.0F);
    ctx.add_border(
        search, theme_color("search-border-color", Color{0.84F, 0.86F, 0.90F, 1.0F}), 1.0F, 8.0F);

    const auto search_font = palette_font(theme_number("title-font-size", 13.0F) * text_scale);
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

    const auto results = results_rect(*this, resolved_search_section_height());
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

    const auto title_font =
        palette_font(theme_number("title-font-size", 13.0F) * text_scale, FontWeight::Medium);
    const auto detail_font = palette_font(theme_number("detail-font-size", 12.0F) * text_scale);
    const float row_height = resolved_row_height();
    const float row_padding_x = theme_number("row-padding-x", 12.0F) * text_scale;
    const float detail_indent = theme_number("detail-indent", 12.0F) * text_scale;
    const float line_spacing = theme_number("line-spacing", 4.0F) * text_scale;
    const float selection_radius = theme_number("selection-radius", 8.0F);
    const auto title_line = measure_text("Ag", title_font);
    const auto detail_line = measure_text("Ag", detail_font);
    const auto first_result = static_cast<std::size_t>(impl_->scroll_offset / row_height);
    float y = results.y - std::fmod(impl_->scroll_offset, row_height);
    for (std::size_t result = first_result; result < impl_->filtered.size() && y < results.bottom();
         ++result) {
        const auto source = impl_->filtered[result];
        const auto& command = impl_->commands[source];
        const Rect row_rect = {
            results.x + 6.0F, y + 2.0F, std::max(0.0F, results.width - 12.0F), row_height - 4.0F};
        const bool current = impl_->current_result && *impl_->current_result == result;
        const bool hovered = impl_->hovered_result == result && command.enabled;
        if (current && command.enabled) {
            ctx.add_rounded_rect(row_rect,
                                 Color{selected_bg.r, selected_bg.g, selected_bg.b, 0.82F},
                                 selection_radius);
            if (focus_visible) {
                ctx.add_border(row_rect,
                               Color{focus_ring.r, focus_ring.g, focus_ring.b, 0.75F},
                               1.0F,
                               selection_radius);
            }
        } else if (hovered) {
            ctx.add_rounded_rect(row_rect, hover_bg, selection_radius);
        }

        const auto command_text = command.enabled ? text_color : disabled_text;
        const auto detail_color = command.enabled ? muted_text : disabled_text;
        ctx.push_rounded_clip({row_rect.x + row_padding_x,
                               row_rect.y,
                               std::max(0.0F, row_rect.width - (row_padding_x * 2.0F)),
                               row_rect.height},
                              selection_radius);
        std::string detail;
        if (!command.category.empty()) {
            detail += command.category;
        }
        if (!command.subtitle.empty()) {
            if (!detail.empty()) {
                detail += " — ";
            }
            detail += command.subtitle;
        }
        if (!command.enabled) {
            if (!detail.empty()) {
                detail += " — ";
            }
            detail += "Disabled";
        }
        const float text_block_height =
            title_line.height + (detail.empty() ? 0.0F : line_spacing + detail_line.height);
        const float title_y =
            row_rect.y + std::max(0.0F, (row_rect.height - text_block_height) * 0.5F);
        const float title_x = row_rect.x + row_padding_x;
        const float title_width = std::max(0.0F, row_rect.right() - row_padding_x - title_x);
        add_text_elided(
            ctx, {title_x, title_y}, command.title, title_width, command_text, title_font);
        if (!detail.empty()) {
            const float detail_x = title_x + detail_indent;
            const float detail_width = std::max(0.0F, row_rect.right() - row_padding_x - detail_x);
            add_text_elided(ctx,
                            {detail_x, title_y + title_line.height + line_spacing},
                            detail,
                            detail_width,
                            detail_color,
                            detail_font);
        }
        ctx.pop_container();
        ctx.add_color_rect({results.x + row_padding_x,
                            y + row_height - 1.0F,
                            std::max(0.0F, results.width - (row_padding_x * 2.0F)),
                            1.0F},
                           row_separator);
        y += row_height;
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
        const auto enabled =
            std::find_if(impl_->filtered.begin(),
                         impl_->filtered.end(),
                         [this](std::size_t source) { return impl_->commands.at(source).enabled; });
        impl_->current_result = enabled == impl_->filtered.end()
                                    ? std::optional<std::size_t>{0}
                                    : std::optional<std::size_t>{static_cast<std::size_t>(
                                          enabled - impl_->filtered.begin())};
    }
    const float row_height = resolved_row_height();
    clamp_scroll_offset(impl_->scroll_offset,
                        impl_->filtered.size(),
                        row_height,
                        results_rect(*this, resolved_search_section_height()).height);
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
    if (delta == 0) {
        return;
    }

    const bool forward = delta > 0;
    auto remaining = static_cast<std::size_t>(std::abs(delta));
    auto result = impl_->current_result;
    while (remaining > 0) {
        std::optional<std::size_t> next;
        if (forward) {
            const std::size_t first = result ? *result + 1 : 0;
            for (std::size_t index = first; index < impl_->filtered.size(); ++index) {
                if (impl_->commands.at(impl_->filtered.at(index)).enabled) {
                    next = index;
                    break;
                }
            }
        } else {
            std::size_t index = result.value_or(impl_->filtered.size());
            while (index > 0) {
                --index;
                if (impl_->commands.at(impl_->filtered.at(index)).enabled) {
                    next = index;
                    break;
                }
            }
        }
        if (!next) {
            break;
        }
        result = next;
        --remaining;
    }
    if (result != impl_->current_result) {
        set_current_result(result);
    }
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
    const auto results = results_rect(*this, resolved_search_section_height());
    const float row_height = resolved_row_height();
    const float row_top = static_cast<float>(*impl_->current_result) * row_height;
    const float row_bottom = row_top + row_height;
    if (row_top < impl_->scroll_offset) {
        impl_->scroll_offset = row_top;
    } else if (row_bottom > impl_->scroll_offset + results.height) {
        impl_->scroll_offset = row_bottom - results.height;
    }
    clamp_scroll_offset(impl_->scroll_offset, impl_->filtered.size(), row_height, results.height);
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
    const auto results = results_rect(*this, resolved_search_section_height());
    if (!results.contains(point) || impl_->filtered.empty()) {
        return std::nullopt;
    }
    const float local_y = point.y - results.y + impl_->scroll_offset;
    const auto result = static_cast<std::size_t>(local_y / resolved_row_height());
    if (result >= impl_->filtered.size()) {
        return std::nullopt;
    }
    return result;
}

float CommandPalette::resolved_row_height() const {
    const float text_scale = theme_number("text-scale", 1.0F);
    const auto title_font =
        palette_font(theme_number("title-font-size", 13.0F) * text_scale, FontWeight::Medium);
    const auto detail_font = palette_font(theme_number("detail-font-size", 12.0F) * text_scale);
    const float text_height = measure_text("Ag", title_font).height +
                              (theme_number("line-spacing", 4.0F) * text_scale) +
                              measure_text("Ag", detail_font).height;
    const float padded_text_height =
        text_height + (theme_number("row-padding-y", 8.0F) * text_scale * 2.0F);
    return std::max(theme_number("row-height", 56.0F) * text_scale, padded_text_height);
}

float CommandPalette::resolved_search_height() const {
    const float text_scale = theme_number("text-scale", 1.0F);
    const auto search_font = palette_font(theme_number("title-font-size", 13.0F) * text_scale);
    const float padded_text_height = measure_text("Ag", search_font).height + (16.0F * text_scale);
    return std::max(theme_number("search-height", 38.0F) * text_scale, padded_text_height);
}

float CommandPalette::resolved_search_section_height() const {
    const float text_scale = theme_number("text-scale", 1.0F);
    return std::max(theme_number("search-section-height", 58.0F) * text_scale,
                    resolved_search_height() + (20.0F * text_scale));
}

} // namespace nk
