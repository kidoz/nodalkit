#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <nk/actions/shortcut.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/platform/platform_backend.h>
#include <nk/platform/spell_checker.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/text_field.h>
#include <optional>
#include <vector>

namespace nk {

namespace {

std::string& fallback_clipboard_text() {
    static std::string clipboard;
    return clipboard;
}

std::string& fallback_primary_selection_text() {
    static std::string selection;
    return selection;
}

FontDescriptor text_field_font() {
    const float scale = Application::instance() != nullptr
                            ? Application::instance()->system_preferences().text_scale_factor
                            : 1.0F;
    return FontDescriptor{
        .family = {},
        .size = 13.5F * scale,
        .weight = FontWeight::Regular,
    };
}

bool has_shift(Modifiers modifiers) {
    return (modifiers & Modifiers::Shift) == Modifiers::Shift;
}

bool has_primary_shortcut(Modifiers modifiers) {
    return (modifiers & Modifiers::Super) == Modifiers::Super ||
           (modifiers & Modifiers::Ctrl) == Modifiers::Ctrl;
}

std::string default_text_field_accessible_name(std::string_view placeholder) {
    return placeholder.empty() ? "Text field" : std::string(placeholder);
}

std::optional<char> key_to_ascii(const KeyEvent& event) {
    const bool shift = has_shift(event.modifiers);
    auto key = static_cast<uint16_t>(event.key);

    if (key >= static_cast<uint16_t>(KeyCode::A) && key <= static_cast<uint16_t>(KeyCode::Z)) {
        const char base = shift ? 'A' : 'a';
        return static_cast<char>(base + (key - static_cast<uint16_t>(KeyCode::A)));
    }

    switch (event.key) {
    case KeyCode::Num1:
        return shift ? '!' : '1';
    case KeyCode::Num2:
        return shift ? '@' : '2';
    case KeyCode::Num3:
        return shift ? '#' : '3';
    case KeyCode::Num4:
        return shift ? '$' : '4';
    case KeyCode::Num5:
        return shift ? '%' : '5';
    case KeyCode::Num6:
        return shift ? '^' : '6';
    case KeyCode::Num7:
        return shift ? '&' : '7';
    case KeyCode::Num8:
        return shift ? '*' : '8';
    case KeyCode::Num9:
        return shift ? '(' : '9';
    case KeyCode::Num0:
        return shift ? ')' : '0';
    case KeyCode::Space:
        return ' ';
    case KeyCode::Minus:
        return shift ? '_' : '-';
    case KeyCode::Equals:
        return shift ? '+' : '=';
    case KeyCode::LeftBracket:
        return shift ? '{' : '[';
    case KeyCode::RightBracket:
        return shift ? '}' : ']';
    case KeyCode::Backslash:
        return shift ? '|' : '\\';
    case KeyCode::Semicolon:
        return shift ? ':' : ';';
    case KeyCode::Apostrophe:
        return shift ? '"' : '\'';
    case KeyCode::Grave:
        return shift ? '~' : '`';
    case KeyCode::Comma:
        return shift ? '<' : ',';
    case KeyCode::Period:
        return shift ? '>' : '.';
    case KeyCode::Slash:
        return shift ? '?' : '/';
    default:
        return std::nullopt;
    }
}

struct Utf8Unit {
    std::size_t byte_index = 0;
    std::size_t next_index = 0;
    char32_t code_point = 0;
};

std::optional<Utf8Unit> decode_utf8_unit(std::string_view text, std::size_t index) {
    if (index >= text.size()) {
        return std::nullopt;
    }

    const auto lead = static_cast<unsigned char>(text[index]);
    if (lead < 0x80) {
        return Utf8Unit{index, index + 1, static_cast<char32_t>(lead)};
    }

    auto continuation = [&](std::size_t offset) -> std::optional<unsigned char> {
        const auto next_index = index + offset;
        if (next_index >= text.size()) {
            return std::nullopt;
        }
        const auto byte = static_cast<unsigned char>(text[next_index]);
        if ((byte & 0xC0U) != 0x80U) {
            return std::nullopt;
        }
        return byte;
    };

    if ((lead & 0xE0U) == 0xC0U) {
        const auto b1 = continuation(1);
        if (!b1.has_value()) {
            return Utf8Unit{index, index + 1, 0xFFFD};
        }
        const char32_t cp = static_cast<char32_t>(((lead & 0x1FU) << 6) | (*b1 & 0x3FU));
        return Utf8Unit{index, index + 2, cp};
    }
    if ((lead & 0xF0U) == 0xE0U) {
        const auto b1 = continuation(1);
        const auto b2 = continuation(2);
        if (!b1.has_value() || !b2.has_value()) {
            return Utf8Unit{index, index + 1, 0xFFFD};
        }
        const char32_t cp =
            static_cast<char32_t>(((lead & 0x0FU) << 12) | ((*b1 & 0x3FU) << 6) | (*b2 & 0x3FU));
        return Utf8Unit{index, index + 3, cp};
    }
    if ((lead & 0xF8U) == 0xF0U) {
        const auto b1 = continuation(1);
        const auto b2 = continuation(2);
        const auto b3 = continuation(3);
        if (!b1.has_value() || !b2.has_value() || !b3.has_value()) {
            return Utf8Unit{index, index + 1, 0xFFFD};
        }
        const char32_t cp = static_cast<char32_t>(((lead & 0x07U) << 18) | ((*b1 & 0x3FU) << 12) |
                                                  ((*b2 & 0x3FU) << 6) | (*b3 & 0x3FU));
        return Utf8Unit{index, index + 4, cp};
    }

    return Utf8Unit{index, index + 1, 0xFFFD};
}

std::vector<Utf8Unit> decode_utf8_units(std::string_view text) {
    std::vector<Utf8Unit> units;
    for (std::size_t index = 0; index < text.size();) {
        const auto unit =
            decode_utf8_unit(text, index).value_or(Utf8Unit{index, index + 1, 0xFFFD});
        units.push_back(unit);
        index = std::max(unit.next_index, index + 1);
    }
    return units;
}

bool is_combining_mark(char32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F);
}

bool is_variation_selector(char32_t cp) {
    return (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0xE0100 && cp <= 0xE01EF);
}

bool is_emoji_modifier(char32_t cp) {
    return cp >= 0x1F3FB && cp <= 0x1F3FF;
}

bool is_regional_indicator(char32_t cp) {
    return cp >= 0x1F1E6 && cp <= 0x1F1FF;
}

bool is_grapheme_extend(char32_t cp) {
    return is_combining_mark(cp) || is_variation_selector(cp) || is_emoji_modifier(cp);
}

std::vector<std::size_t> grapheme_boundaries(std::string_view text) {
    std::vector<std::size_t> boundaries;
    boundaries.push_back(0);

    const auto units = decode_utf8_units(text);
    if (units.empty()) {
        return boundaries;
    }

    std::size_t index = 0;
    while (index < units.size()) {
        std::size_t next = index + 1;

        if (is_regional_indicator(units[index].code_point)) {
            if (next < units.size() && is_regional_indicator(units[next].code_point)) {
                ++next;
            }
        } else {
            while (next < units.size()) {
                const auto cp = units[next].code_point;
                if (is_grapheme_extend(cp)) {
                    ++next;
                    continue;
                }
                if (cp == 0x200D) {
                    ++next;
                    if (next < units.size()) {
                        ++next;
                        while (next < units.size() && is_grapheme_extend(units[next].code_point)) {
                            ++next;
                        }
                    }
                    continue;
                }
                break;
            }
        }

        const std::size_t boundary = next < units.size() ? units[next].byte_index : text.size();
        boundaries.push_back(boundary);
        index = next;
    }

    return boundaries;
}

std::size_t previous_grapheme_boundary(std::string_view text, std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    const auto it = std::lower_bound(boundaries.begin(), boundaries.end(), position);
    if (it == boundaries.begin()) {
        return 0;
    }
    if (it != boundaries.end() && *it == position) {
        return *std::prev(it);
    }
    return *std::prev(it);
}

std::size_t next_grapheme_boundary(std::string_view text, std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    const auto it = std::upper_bound(boundaries.begin(), boundaries.end(), position);
    return it != boundaries.end() ? *it : text.size();
}

std::size_t nearest_grapheme_boundary(std::string_view text, std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    const auto it = std::lower_bound(boundaries.begin(), boundaries.end(), position);
    if (it == boundaries.end()) {
        return text.size();
    }
    if (*it == position || it == boundaries.begin()) {
        return *it;
    }
    const auto prev = *std::prev(it);
    return (position - prev) <= (*it - position) ? prev : *it;
}

bool is_unicode_whitespace(char32_t cp) {
    switch (cp) {
    case 0x0009:
    case 0x000A:
    case 0x000B:
    case 0x000C:
    case 0x000D:
    case 0x0020:
    case 0x0085:
    case 0x00A0:
    case 0x1680:
    case 0x2000:
    case 0x2001:
    case 0x2002:
    case 0x2003:
    case 0x2004:
    case 0x2005:
    case 0x2006:
    case 0x2007:
    case 0x2008:
    case 0x2009:
    case 0x200A:
    case 0x2028:
    case 0x2029:
    case 0x202F:
    case 0x205F:
    case 0x3000:
        return true;
    default:
        return false;
    }
}

enum class GraphemeKind : uint8_t {
    Word,
    Space,
    Punctuation,
};

GraphemeKind classify_grapheme(char32_t cp) {
    if (is_unicode_whitespace(cp)) {
        return GraphemeKind::Space;
    }
    if (cp > 0x7FU) {
        return GraphemeKind::Word;
    }
    if (std::isalnum(static_cast<unsigned char>(cp)) != 0 || cp == '_') {
        return GraphemeKind::Word;
    }
    return GraphemeKind::Punctuation;
}

std::vector<GraphemeKind> grapheme_kinds(std::string_view text,
                                         const std::vector<std::size_t>& boundaries) {
    std::vector<GraphemeKind> kinds;
    kinds.reserve(boundaries.size() > 0 ? boundaries.size() - 1 : 0);
    for (std::size_t i = 0; i + 1 < boundaries.size(); ++i) {
        const auto unit = decode_utf8_unit(text, boundaries[i])
                              .value_or(Utf8Unit{boundaries[i], boundaries[i] + 1, 0xFFFD});
        kinds.push_back(classify_grapheme(unit.code_point));
    }
    return kinds;
}

std::size_t previous_word_boundary(std::string_view text, std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    if (boundaries.size() <= 1 || position == 0) {
        return 0;
    }

    const auto kinds = grapheme_kinds(text, boundaries);
    auto it = std::lower_bound(boundaries.begin(), boundaries.end(), position);
    std::size_t cluster = 0;
    if (it == boundaries.end()) {
        cluster = kinds.size();
    } else if (*it == position) {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
    } else {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
    }

    while (cluster > 0 && kinds[cluster - 1] == GraphemeKind::Space) {
        --cluster;
    }
    if (cluster == 0) {
        return 0;
    }

    while (cluster > 0 && kinds[cluster - 1] != GraphemeKind::Word) {
        --cluster;
    }
    while (cluster > 0 && kinds[cluster - 1] == GraphemeKind::Word) {
        --cluster;
    }
    return boundaries[cluster];
}

std::size_t next_word_boundary(std::string_view text, std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    if (boundaries.size() <= 1 || position >= text.size()) {
        return text.size();
    }

    const auto kinds = grapheme_kinds(text, boundaries);
    auto it = std::lower_bound(boundaries.begin(), boundaries.end(), position);
    std::size_t cluster = 0;
    if (it == boundaries.end()) {
        return text.size();
    }
    if (*it == position) {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
    } else {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
    }
    if (cluster >= kinds.size()) {
        return text.size();
    }

    if (kinds[cluster] != GraphemeKind::Word) {
        while (cluster < kinds.size() && kinds[cluster] != GraphemeKind::Word) {
            ++cluster;
        }
    }
    while (cluster < kinds.size() && kinds[cluster] == GraphemeKind::Word) {
        ++cluster;
    }
    return boundaries[cluster];
}

std::pair<std::size_t, std::size_t> word_selection_range(std::string_view text,
                                                         std::size_t position) {
    const auto boundaries = grapheme_boundaries(text);
    if (boundaries.size() <= 1) {
        return {0, text.size()};
    }

    const auto kinds = grapheme_kinds(text, boundaries);
    auto it = std::lower_bound(boundaries.begin(), boundaries.end(), position);
    std::size_t cluster = 0;
    if (it == boundaries.end()) {
        cluster = kinds.size() - 1;
    } else if (*it == position) {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
        if (cluster == kinds.size()) {
            cluster = kinds.size() - 1;
        } else if (cluster > 0) {
            --cluster;
        }
    } else {
        cluster = static_cast<std::size_t>(std::distance(boundaries.begin(), it));
        if (cluster > 0) {
            --cluster;
        }
    }

    if (kinds[cluster] != GraphemeKind::Word) {
        std::size_t forward = cluster;
        while (forward < kinds.size() && kinds[forward] != GraphemeKind::Word) {
            ++forward;
        }
        if (forward < kinds.size()) {
            cluster = forward;
        } else {
            std::size_t backward = cluster;
            while (backward > 0 && kinds[backward] != GraphemeKind::Word) {
                --backward;
            }
            if (kinds[backward] == GraphemeKind::Word) {
                cluster = backward;
            } else {
                return {boundaries[cluster], boundaries[cluster + 1]};
            }
        }
    }

    std::size_t start_cluster = cluster;
    while (start_cluster > 0 && kinds[start_cluster - 1] == GraphemeKind::Word) {
        --start_cluster;
    }
    std::size_t end_cluster = cluster + 1;
    while (end_cluster < kinds.size() && kinds[end_cluster] == GraphemeKind::Word) {
        ++end_cluster;
    }
    return {boundaries[start_cluster], boundaries[end_cluster]};
}

} // namespace

struct TextField::Impl {
    struct HistoryState {
        std::string text;
        std::size_t cursor = 0;
        std::size_t selection_anchor = 0;
    };

    enum class HistoryGroup : uint8_t {
        None,
        Insert,
        DeleteBackward,
        DeleteForward,
    };

    std::string text;
    std::string placeholder;
    bool editable = true;
    std::size_t cursor = 0;
    std::size_t selection_anchor = 0;
    std::string preedit_text;
    std::size_t preedit_selection_start = 0;
    std::size_t preedit_selection_end = 0;
    bool selecting_with_mouse = false;
    bool selecting_word_with_mouse = false;
    bool selecting_line_with_mouse = false;
    std::size_t mouse_selection_base_start = 0;
    std::size_t mouse_selection_base_end = 0;
    float scroll_x = 0.0F;
    std::vector<HistoryState> history;
    std::size_t history_index = 0;
    HistoryGroup history_group = HistoryGroup::None;
    bool spell_check_enabled = false;
    mutable std::string spell_check_cache_text;
    mutable bool spell_check_cache_valid = false;
    mutable std::vector<SpellCheckRange> spell_check_ranges;
    Signal<std::string_view> text_changed;
    Signal<> activate;
};

std::shared_ptr<TextField> TextField::create(std::string initial_text) {
    return std::shared_ptr<TextField>(new TextField(std::move(initial_text)));
}

TextField::TextField(std::string text) : impl_(std::make_unique<Impl>()) {
    impl_->text = std::move(text);
    impl_->cursor = impl_->text.size();
    impl_->selection_anchor = impl_->cursor;
    set_focusable(true);
    add_style_class("text-field");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::TextInput);
    accessible.set_name(default_text_field_accessible_name(impl_->placeholder));
    accessible.set_value(impl_->text);
    reset_history();
}

TextField::~TextField() = default;

std::string_view TextField::text() const {
    return impl_->text;
}

void TextField::set_text(std::string text) {
    if (impl_->text != text) {
        reset_mouse_selection_state();
        impl_->text = std::move(text);
        ensure_accessible().set_value(impl_->text);
        impl_->cursor = impl_->text.size();
        impl_->selection_anchor = impl_->cursor;
        clear_preedit();
        impl_->scroll_x = 0.0F;
        sync_primary_selection_ownership();
        reset_history();
        impl_->text_changed.emit(impl_->text);
        queue_text_redraw();
    }
}

std::string_view TextField::placeholder() const {
    return impl_->placeholder;
}

void TextField::set_placeholder(std::string placeholder) {
    impl_->placeholder = std::move(placeholder);
    ensure_accessible().set_description(impl_->placeholder);
    ensure_accessible().set_name(default_text_field_accessible_name(impl_->placeholder));
    queue_text_redraw();
}

bool TextField::is_editable() const {
    return impl_->editable;
}

void TextField::set_editable(bool editable) {
    if (impl_->editable == editable) {
        return;
    }
    impl_->editable = editable;
    queue_text_redraw();
}

std::size_t TextField::cursor_position() const {
    return impl_->cursor;
}

std::size_t TextField::selection_start() const {
    return std::min(impl_->cursor, impl_->selection_anchor);
}

std::size_t TextField::selection_end() const {
    return std::max(impl_->cursor, impl_->selection_anchor);
}

bool TextField::has_selection() const {
    return impl_->cursor != impl_->selection_anchor;
}

void TextField::select_all() {
    clear_preedit();
    reset_history_grouping();
    impl_->selection_anchor = 0;
    impl_->cursor = impl_->text.size();
    sync_primary_selection_ownership();
    ensure_caret_visible();
    queue_text_redraw();
}

void TextField::set_spell_check_enabled(bool enabled) {
    if (impl_->spell_check_enabled == enabled) {
        return;
    }
    impl_->spell_check_enabled = enabled;
    impl_->spell_check_cache_valid = false;
    impl_->spell_check_ranges.clear();
    queue_redraw();
}

bool TextField::is_spell_check_enabled() const {
    return impl_->spell_check_enabled;
}

Signal<std::string_view>& TextField::on_text_changed() {
    return impl_->text_changed;
}

Signal<>& TextField::on_activate() {
    return impl_->activate;
}

SizeRequest TextField::measure(const Constraints& /*constraints*/) const {
    const float w = theme_number("min-width", 240.0F);
    const float scale = Application::instance() != nullptr
                            ? Application::instance()->system_preferences().text_scale_factor
                            : 1.0F;
    const float h = theme_number("min-height", 36.0F) * scale;
    return {120.0F, h, w, h};
}

void TextField::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    ensure_caret_visible();
}

bool TextField::handle_mouse_event(const MouseEvent& event) {
    const auto point = Point{event.x, event.y};

    switch (event.type) {
    case MouseEvent::Type::Press:
        if (event.button != 1 || !allocation().contains(point)) {
            if (event.button == 3 && allocation().contains(point)) {
                clear_preedit();
                reset_history_grouping();
                return impl_->editable && paste_from_primary_selection(hit_test_cursor(point));
            }
            return false;
        }
        clear_preedit();
        reset_history_grouping();
        impl_->selecting_word_with_mouse = false;
        impl_->selecting_line_with_mouse = false;
        if (event.click_count >= 3) {
            impl_->selecting_with_mouse = true;
            impl_->selecting_line_with_mouse = true;
            impl_->selection_anchor = 0;
            impl_->cursor = impl_->text.size();
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        if (event.click_count >= 2) {
            const auto [start, end] = word_selection_range(impl_->text, hit_test_cursor(point));
            impl_->selecting_with_mouse = true;
            impl_->selecting_word_with_mouse = true;
            impl_->mouse_selection_base_start = start;
            impl_->mouse_selection_base_end = end;
            impl_->selection_anchor = start;
            impl_->cursor = end;
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        impl_->selecting_with_mouse = true;
        move_cursor(hit_test_cursor(point), has_shift(event.modifiers));
        return true;
    case MouseEvent::Type::Move:
        if (!impl_->selecting_with_mouse) {
            return false;
        }
        if (impl_->selecting_line_with_mouse) {
            impl_->selection_anchor = 0;
            impl_->cursor = impl_->text.size();
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        if (impl_->selecting_word_with_mouse) {
            const auto [start, end] = word_selection_range(impl_->text, hit_test_cursor(point));
            if (end <= impl_->mouse_selection_base_start) {
                impl_->selection_anchor = impl_->mouse_selection_base_end;
                impl_->cursor = start;
            } else {
                impl_->selection_anchor = impl_->mouse_selection_base_start;
                impl_->cursor = end;
            }
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        move_cursor(hit_test_cursor(point), true);
        return true;
    case MouseEvent::Type::Release:
        if (event.button != 1 || !impl_->selecting_with_mouse) {
            return false;
        }
        if (impl_->selecting_line_with_mouse) {
            impl_->selection_anchor = 0;
            impl_->cursor = impl_->text.size();
            impl_->selecting_with_mouse = false;
            impl_->selecting_line_with_mouse = false;
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        if (impl_->selecting_word_with_mouse) {
            const auto [start, end] = word_selection_range(impl_->text, hit_test_cursor(point));
            if (end <= impl_->mouse_selection_base_start) {
                impl_->selection_anchor = impl_->mouse_selection_base_end;
                impl_->cursor = start;
            } else {
                impl_->selection_anchor = impl_->mouse_selection_base_start;
                impl_->cursor = end;
            }
            impl_->selecting_with_mouse = false;
            impl_->selecting_word_with_mouse = false;
            sync_primary_selection_ownership();
            ensure_caret_visible();
            queue_text_redraw();
            return true;
        }
        impl_->selecting_with_mouse = false;
        move_cursor(hit_test_cursor(point), true);
        return true;
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool TextField::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (event.key == KeyCode::Return) {
        reset_history_grouping();
        impl_->activate.emit();
        return true;
    }

    if (has_primary_shortcut(event.modifiers) &&
        (event.modifiers & Modifiers::Alt) != Modifiers::Alt) {
        switch (event.key) {
        case KeyCode::A:
            select_all();
            return true;
        case KeyCode::C:
            copy_selection_to_clipboard();
            return has_selection();
        case KeyCode::X:
            if (!impl_->editable || !has_selection()) {
                return false;
            }
            reset_history_grouping();
            copy_selection_to_clipboard();
            replace_selection({}, true, false);
            return true;
        case KeyCode::V:
            return impl_->editable && paste_from_clipboard();
        case KeyCode::Z:
            if ((event.modifiers & Modifiers::Shift) == Modifiers::Shift) {
                return redo();
            }
            return undo();
        case KeyCode::Y:
            return redo();
        default:
            break;
        }
    }

    switch (event.key) {
    case KeyCode::Left:
        clear_preedit();
        reset_history_grouping();
        if (!has_shift(event.modifiers) && has_selection()) {
            move_cursor(selection_start(), false);
        } else if ((event.modifiers & Modifiers::Alt) == Modifiers::Alt && impl_->cursor > 0) {
            move_cursor(previous_word_boundary(impl_->text, impl_->cursor),
                        has_shift(event.modifiers));
        } else if (impl_->cursor > 0) {
            move_cursor(previous_grapheme_boundary(impl_->text, impl_->cursor),
                        has_shift(event.modifiers));
        }
        return true;
    case KeyCode::Right:
        clear_preedit();
        reset_history_grouping();
        if (!has_shift(event.modifiers) && has_selection()) {
            move_cursor(selection_end(), false);
        } else if ((event.modifiers & Modifiers::Alt) == Modifiers::Alt &&
                   impl_->cursor < impl_->text.size()) {
            move_cursor(next_word_boundary(impl_->text, impl_->cursor), has_shift(event.modifiers));
        } else if (impl_->cursor < impl_->text.size()) {
            move_cursor(next_grapheme_boundary(impl_->text, impl_->cursor),
                        has_shift(event.modifiers));
        }
        return true;
    case KeyCode::Home:
        clear_preedit();
        reset_history_grouping();
        move_cursor(0, has_shift(event.modifiers));
        return true;
    case KeyCode::End:
        clear_preedit();
        reset_history_grouping();
        move_cursor(impl_->text.size(), has_shift(event.modifiers));
        return true;
    case KeyCode::Backspace:
        clear_preedit();
        return impl_->editable &&
               (((event.modifiers & Modifiers::Alt) == Modifiers::Alt) ? delete_backward_word()
                                                                       : delete_backward());
    case KeyCode::Delete:
        clear_preedit();
        return impl_->editable &&
               (((event.modifiers & Modifiers::Alt) == Modifiers::Alt) ? delete_forward_word()
                                                                       : delete_forward());
    default:
        break;
    }

    if (!impl_->editable || has_primary_shortcut(event.modifiers) ||
        (event.modifiers & Modifiers::Alt) == Modifiers::Alt) {
        return false;
    }

    auto character = key_to_ascii(event);
    if (!character.has_value()) {
        reset_history_grouping();
        return false;
    }

    std::string inserted(1, *character);
    impl_->history_group = Impl::HistoryGroup::Insert;
    replace_selection(inserted, true, true);
    return true;
}

bool TextField::handle_text_input_event(const TextInputEvent& event) {
    switch (event.type) {
    case TextInputEvent::Type::ClearPreedit:
        clear_preedit();
        return true;
    case TextInputEvent::Type::Preedit:
        reset_history_grouping();
        impl_->preedit_text = event.text;
        impl_->preedit_selection_start =
            std::min(event.selection_start, impl_->preedit_text.size());
        impl_->preedit_selection_end = std::min(event.selection_end, impl_->preedit_text.size());
        ensure_caret_visible();
        queue_text_redraw();
        return true;
    case TextInputEvent::Type::Commit:
        clear_preedit();
        if (!impl_->editable || event.text.empty()) {
            return false;
        }
        impl_->history_group = event.text.size() == 1 && !has_selection()
                                   ? Impl::HistoryGroup::Insert
                                   : Impl::HistoryGroup::None;
        replace_selection(event.text, true, event.text.size() == 1 && !has_selection());
        return true;
    case TextInputEvent::Type::DeleteSurrounding:
        if (!impl_->editable) {
            return false;
        }
        return delete_surrounding_text(event.delete_before_length, event.delete_after_length);
    }

    return false;
}

std::optional<Rect> TextField::text_input_caret_rect() const {
    const auto text_bounds = text_rect();
    const auto font = text_field_font();
    const auto display_text = composed_display_text();
    const float caret_x =
        text_bounds.x + measure_text(display_text.substr(0, display_caret_position()), font).width -
        impl_->scroll_x;
    return Rect{
        caret_x,
        text_bounds.y + 4.0F,
        1.5F,
        std::max(0.0F, text_bounds.height - 8.0F),
    };
}

CursorShape TextField::cursor_shape() const {
    return impl_->editable ? CursorShape::IBeam : CursorShape::Default;
}

void TextField::on_focus_changed(bool focused) {
    if (!focused) {
        impl_->selecting_with_mouse = false;
        impl_->selecting_line_with_mouse = false;
        clear_preedit();
        reset_history_grouping();
    }
    ensure_caret_visible();
}

void TextField::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const auto body = inner_body_rect();
    const auto text_bounds = text_rect();
    const float corner_radius = theme_number("corner-radius", 10.0F);

    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a,
                             theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
                             corner_radius + 2.0F);
    }

    ctx.add_rounded_rect(
        body, theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}), 1.0F, corner_radius);

    const auto display_text = composed_display_text();
    Color text_color = impl_->text.empty()
                           ? theme_color("placeholder-color", Color{0.55F, 0.58F, 0.62F, 1.0F})
                           : theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
    if (!display_text.empty()) {
        const auto font = text_field_font();
        const auto measured = measure_text(display_text, font);
        const float text_y =
            text_bounds.y + std::max(0.0F, (text_bounds.height - measured.height) * 0.5F);

        if (!impl_->text.empty() && has_selection()) {
            const auto selection_bg =
                theme_color("selection-background-color", Color{0.3F, 0.56F, 0.9F, 0.24F});
            const auto selection_left =
                measure_text(impl_->text.substr(0, selection_start()), font).width;
            const auto selection_width =
                measure_text(
                    impl_->text.substr(selection_start(), selection_end() - selection_start()),
                    font)
                    .width;
            ctx.add_rounded_rect({text_bounds.x + selection_left - impl_->scroll_x,
                                  text_bounds.y + 2.0F,
                                  selection_width,
                                  std::max(0.0F, text_bounds.height - 4.0F)},
                                 selection_bg,
                                 6.0F);
        }

        ctx.push_rounded_clip(body, corner_radius);
        ctx.add_text({text_bounds.x - impl_->scroll_x, text_y}, display_text, text_color, font);

        if (impl_->spell_check_enabled && impl_->preedit_text.empty() && !impl_->text.empty()) {
            if (!impl_->spell_check_cache_valid || impl_->spell_check_cache_text != impl_->text) {
                impl_->spell_check_ranges.clear();
                if (auto* app = Application::instance(); app != nullptr) {
                    if (auto* checker = app->platform_backend().spell_checker();
                        checker != nullptr) {
                        impl_->spell_check_ranges = checker->check(impl_->text);
                    }
                }
                impl_->spell_check_cache_text = impl_->text;
                impl_->spell_check_cache_valid = true;
            }

            const Color misspelling_color =
                theme_color("misspelling-underline-color", Color{0.92F, 0.25F, 0.25F, 1.0F});
            const float underline_y = text_bounds.bottom() - 2.0F;
            for (const auto& range : impl_->spell_check_ranges) {
                if (range.length == 0 || range.start + range.length > impl_->text.size()) {
                    continue;
                }
                const float left = measure_text(impl_->text.substr(0, range.start), font).width;
                const float width =
                    measure_text(impl_->text.substr(range.start, range.length), font).width;
                ctx.add_color_rect({text_bounds.x + left - impl_->scroll_x,
                                    underline_y,
                                    width,
                                    1.5F},
                                   misspelling_color);
            }
        }

        if (!impl_->preedit_text.empty()) {
            const float preedit_x = text_bounds.x +
                                    measure_text(impl_->text.substr(0, impl_->cursor), font).width -
                                    impl_->scroll_x;
            const float preedit_width = measure_text(impl_->preedit_text, font).width;
            const float underline_y = text_bounds.bottom() - 3.0F;
            ctx.add_color_rect({preedit_x, underline_y, preedit_width, 1.5F},
                               theme_color("caret-color", text_color));
        }
        if (has_flag(state_flags(), StateFlags::Focused)) {
            const auto caret_x =
                text_bounds.x +
                measure_text(display_text.substr(0, display_caret_position()), font).width -
                impl_->scroll_x;
            ctx.add_color_rect(
                {caret_x, text_bounds.y + 4.0F, 1.5F, std::max(0.0F, text_bounds.height - 8.0F)},
                theme_color("caret-color", text_color));
        }
        ctx.pop_container();
    } else if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_color_rect(
            {text_bounds.x, text_bounds.y + 4.0F, 1.5F, std::max(0.0F, text_bounds.height - 8.0F)},
            theme_color("caret-color", text_color));
    }
}

Rect TextField::inner_body_rect() const {
    auto body = allocation();
    if (has_flag(state_flags(), StateFlags::Focused)) {
        body = {body.x + 2.0F,
                body.y + 2.0F,
                std::max(0.0F, body.width - 4.0F),
                std::max(0.0F, body.height - 4.0F)};
    }
    return body;
}

Rect TextField::text_rect() const {
    const auto body = inner_body_rect();
    return {body.x + 12.0F,
            body.y + 4.0F,
            std::max(0.0F, body.width - 24.0F),
            std::max(0.0F, body.height - 8.0F)};
}

Rect TextField::local_text_damage_rect() const {
    const auto a = allocation();
    const auto text_bounds = text_rect();
    if (a.width <= 0.0F || a.height <= 0.0F || text_bounds.width <= 0.0F || text_bounds.height <= 0.0F) {
        return {};
    }

    const float margin = 2.0F;
    const float x = std::max(0.0F, (text_bounds.x - a.x) - margin);
    const float y = std::max(0.0F, (text_bounds.y - a.y) - margin);
    const float right = std::min(a.width, (text_bounds.right() - a.x) + margin);
    const float bottom = std::min(a.height, (text_bounds.bottom() - a.y) + margin);
    return {x, y, std::max(0.0F, right - x), std::max(0.0F, bottom - y)};
}

void TextField::queue_text_redraw() {
    const auto damage = local_text_damage_rect();
    if (damage.width <= 0.0F || damage.height <= 0.0F) {
        queue_redraw();
    } else {
        queue_redraw(damage);
    }
}

std::size_t TextField::hit_test_cursor(Point point) const {
    const auto bounds = text_rect();
    const auto font = text_field_font();
    const float local_x = std::max(0.0F, point.x - bounds.x + impl_->scroll_x);
    const auto boundaries = grapheme_boundaries(impl_->text);

    std::size_t best_index = 0;
    float best_distance = std::numeric_limits<float>::infinity();
    for (const auto index : boundaries) {
        const float caret_x = measure_text(impl_->text.substr(0, index), font).width;
        const float distance = std::fabs(caret_x - local_x);
        if (distance < best_distance) {
            best_distance = distance;
            best_index = index;
        }
    }
    return best_index;
}

void TextField::move_cursor(std::size_t position, bool extend_selection) {
    position = nearest_grapheme_boundary(impl_->text, std::min(position, impl_->text.size()));
    impl_->cursor = position;
    if (!extend_selection) {
        impl_->selection_anchor = position;
    }
    sync_primary_selection_ownership();
    ensure_caret_visible();
    queue_text_redraw();
}

void TextField::replace_selection(std::string_view text,
                                  bool record_history,
                                  bool coalesce_history) {
    reset_mouse_selection_state();
    clear_preedit();
    const auto cursor_before = impl_->cursor;
    const auto anchor_before = impl_->selection_anchor;
    const auto history_group = impl_->history_group;
    const auto start = selection_start();
    const auto end = selection_end();
    const auto had_selection = has_selection();
    const auto changing_text = had_selection || !text.empty();
    if (!changing_text) {
        return;
    }

    if (!had_selection) {
        impl_->selection_anchor = impl_->cursor;
    }

    impl_->text.replace(start, end - start, text);
    impl_->cursor = start + text.size();
    impl_->selection_anchor = impl_->cursor;
    ensure_caret_visible();
    if (record_history) {
        const bool cursor_matches_history = cursor_before == impl_->history.back().cursor;
        const bool anchor_matches_history = anchor_before == impl_->history.back().selection_anchor;
        const bool has_internal_delete_selection =
            history_group == Impl::HistoryGroup::DeleteBackward ||
            history_group == Impl::HistoryGroup::DeleteForward;
        const bool selection_matches_group =
            has_internal_delete_selection ||
            (!had_selection && cursor_before == anchor_before && anchor_matches_history);
        const bool can_coalesce = coalesce_history && history_group != Impl::HistoryGroup::None &&
                                  impl_->history_index + 1 == impl_->history.size() &&
                                  impl_->history.size() > 1 && cursor_matches_history &&
                                  selection_matches_group;

        if (can_coalesce) {
            auto& state = impl_->history.back();
            state.text = impl_->text;
            state.cursor = impl_->cursor;
            state.selection_anchor = impl_->selection_anchor;
        } else {
            push_history_state();
        }
        impl_->history_group = coalesce_history ? history_group : Impl::HistoryGroup::None;
    }
    sync_primary_selection_ownership();
    impl_->text_changed.emit(impl_->text);
    queue_text_redraw();
}

void TextField::ensure_caret_visible() {
    const auto bounds = text_rect();
    const auto font = text_field_font();
    const float content_width = std::max(0.0F, bounds.width);
    const auto display_text = composed_display_text();
    const float caret_x =
        measure_text(display_text.substr(0, display_caret_position()), font).width;
    const float total_width = measure_text(display_text, font).width;
    const float max_scroll = std::max(0.0F, total_width - content_width);

    if (caret_x < impl_->scroll_x) {
        impl_->scroll_x = caret_x;
    } else if (caret_x > impl_->scroll_x + content_width - 2.0F) {
        impl_->scroll_x = caret_x - std::max(0.0F, content_width - 2.0F);
    }

    impl_->scroll_x = std::clamp(impl_->scroll_x, 0.0F, max_scroll);
}

void TextField::reset_history_grouping() {
    impl_->history_group = Impl::HistoryGroup::None;
}

void TextField::reset_history() {
    impl_->history.clear();
    impl_->history.push_back({impl_->text, impl_->cursor, impl_->selection_anchor});
    impl_->history_index = 0;
    reset_history_grouping();
}

void TextField::push_history_state() {
    if (impl_->history_index + 1 < impl_->history.size()) {
        impl_->history.erase(impl_->history.begin() +
                                 static_cast<std::ptrdiff_t>(impl_->history_index + 1),
                             impl_->history.end());
    }

    const auto state = Impl::HistoryState{
        impl_->text,
        impl_->cursor,
        impl_->selection_anchor,
    };
    if (!impl_->history.empty()) {
        const auto& current = impl_->history.back();
        if (current.text == state.text && current.cursor == state.cursor &&
            current.selection_anchor == state.selection_anchor) {
            return;
        }
    }

    impl_->history.push_back(state);
    impl_->history_index = impl_->history.size() - 1;
    reset_history_grouping();
}

bool TextField::undo() {
    if (impl_->history_index == 0) {
        return false;
    }

    reset_mouse_selection_state();
    reset_history_grouping();
    --impl_->history_index;
    const auto& state = impl_->history[impl_->history_index];
    impl_->text = state.text;
    impl_->cursor = state.cursor;
    impl_->selection_anchor = state.selection_anchor;
    sync_primary_selection_ownership();
    ensure_caret_visible();
    impl_->text_changed.emit(impl_->text);
    queue_text_redraw();
    return true;
}

bool TextField::redo() {
    if (impl_->history_index + 1 >= impl_->history.size()) {
        return false;
    }

    reset_mouse_selection_state();
    reset_history_grouping();
    ++impl_->history_index;
    const auto& state = impl_->history[impl_->history_index];
    impl_->text = state.text;
    impl_->cursor = state.cursor;
    impl_->selection_anchor = state.selection_anchor;
    sync_primary_selection_ownership();
    ensure_caret_visible();
    impl_->text_changed.emit(impl_->text);
    queue_text_redraw();
    return true;
}

void TextField::copy_selection_to_clipboard() const {
    if (!has_selection()) {
        return;
    }

    const auto selected =
        impl_->text.substr(selection_start(), selection_end() - selection_start());
    if (auto* app = Application::instance()) {
        app->set_clipboard_text(selected);
    } else {
        fallback_clipboard_text() = selected;
    }
}

void TextField::sync_primary_selection_ownership() const {
    std::string selected;
    if (has_selection()) {
        selected = impl_->text.substr(selection_start(), selection_end() - selection_start());
    }

    if (auto* app = Application::instance()) {
        app->set_primary_selection_text(std::move(selected));
    } else {
        fallback_primary_selection_text() = std::move(selected);
    }
}

bool TextField::delete_backward() {
    if (has_selection()) {
        reset_history_grouping();
        replace_selection({}, true, false);
        return true;
    }
    if (impl_->cursor == 0) {
        return false;
    }

    impl_->history_group = Impl::HistoryGroup::DeleteBackward;
    impl_->selection_anchor = previous_grapheme_boundary(impl_->text, impl_->cursor);
    replace_selection({}, true, true);
    return true;
}

bool TextField::delete_forward() {
    if (has_selection()) {
        reset_history_grouping();
        replace_selection({}, true, false);
        return true;
    }
    if (impl_->cursor >= impl_->text.size()) {
        return false;
    }

    impl_->history_group = Impl::HistoryGroup::DeleteForward;
    impl_->selection_anchor = next_grapheme_boundary(impl_->text, impl_->cursor);
    replace_selection({}, true, true);
    return true;
}

bool TextField::delete_backward_word() {
    if (has_selection()) {
        reset_history_grouping();
        replace_selection({}, true, false);
        return true;
    }
    if (impl_->cursor == 0) {
        return false;
    }

    reset_history_grouping();
    impl_->selection_anchor = previous_word_boundary(impl_->text, impl_->cursor);
    replace_selection({}, true, false);
    return true;
}

bool TextField::delete_forward_word() {
    if (has_selection()) {
        reset_history_grouping();
        replace_selection({}, true, false);
        return true;
    }
    if (impl_->cursor >= impl_->text.size()) {
        return false;
    }

    reset_history_grouping();
    impl_->selection_anchor = next_word_boundary(impl_->text, impl_->cursor);
    replace_selection({}, true, false);
    return true;
}

bool TextField::paste_from_clipboard() {
    std::string text;
    if (auto* app = Application::instance()) {
        text = app->clipboard_text();
    } else {
        text = fallback_clipboard_text();
    }

    if (text.empty()) {
        return false;
    }

    reset_history_grouping();
    replace_selection(text, true, false);
    return true;
}

bool TextField::paste_from_primary_selection(std::optional<std::size_t> cursor_position) {
    std::string text;
    if (auto* app = Application::instance()) {
        text = app->primary_selection_text();
    } else {
        text = fallback_primary_selection_text();
    }

    if (text.empty()) {
        return false;
    }

    reset_history_grouping();
    if (cursor_position.has_value()) {
        reset_mouse_selection_state();
        const auto bounded =
            nearest_grapheme_boundary(impl_->text, std::min(*cursor_position, impl_->text.size()));
        impl_->cursor = bounded;
        impl_->selection_anchor = bounded;
    }
    replace_selection(text, true, false);
    return true;
}

void TextField::clear_preedit() {
    if (impl_->preedit_text.empty() && impl_->preedit_selection_start == 0 &&
        impl_->preedit_selection_end == 0) {
        return;
    }
    impl_->preedit_text.clear();
    impl_->preedit_selection_start = 0;
    impl_->preedit_selection_end = 0;
    queue_text_redraw();
}

void TextField::reset_mouse_selection_state() {
    impl_->selecting_with_mouse = false;
    impl_->selecting_word_with_mouse = false;
    impl_->selecting_line_with_mouse = false;
}

bool TextField::delete_surrounding_text(std::size_t before_length, std::size_t after_length) {
    if (before_length == 0 && after_length == 0) {
        return false;
    }

    const std::size_t safe_before = std::min(before_length, impl_->cursor);
    const std::size_t safe_after = std::min(after_length, impl_->text.size() - impl_->cursor);
    if (safe_before == 0 && safe_after == 0) {
        return false;
    }

    reset_mouse_selection_state();
    reset_history_grouping();
    push_history_state();
    const std::size_t start = impl_->cursor - safe_before;
    const std::size_t end = impl_->cursor + safe_after;
    impl_->text.erase(start, end - start);
    impl_->cursor = start;
    impl_->selection_anchor = start;
    sync_primary_selection_ownership();
    ensure_caret_visible();
    queue_text_redraw();
    impl_->text_changed.emit(impl_->text);
    return true;
}

std::string TextField::composed_display_text() const {
    if (impl_->preedit_text.empty()) {
        return impl_->text.empty() ? impl_->placeholder : impl_->text;
    }

    std::string display = impl_->text;
    display.insert(impl_->cursor, impl_->preedit_text);
    return display;
}

std::size_t TextField::display_caret_position() const {
    if (impl_->preedit_text.empty()) {
        return impl_->cursor;
    }

    return impl_->cursor + std::min(impl_->preedit_selection_end, impl_->preedit_text.size());
}

} // namespace nk
