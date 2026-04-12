#include <algorithm>
#include <cmath>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/text_area.h>
#include <string>
#include <vector>

namespace nk {

namespace {

FontDescriptor text_area_font() {
    return FontDescriptor{
        .family = {},
        .size = 13.5F,
        .weight = FontWeight::Regular,
    };
}

std::vector<std::string_view> split_lines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto pos = text.find('\n', start);
        if (pos == std::string_view::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, pos - start));
        start = pos + 1;
        if (start == text.size()) {
            lines.emplace_back();
        }
    }
    if (lines.empty()) {
        lines.emplace_back();
    }
    return lines;
}

} // namespace

struct TextArea::Impl {
    std::string text;
    std::string placeholder;
    bool editable = true;
    int visible_rows = 4;
    Signal<> text_changed;
    std::size_t cursor_pos = 0;
    float scroll_offset = 0.0F;
    bool focused_state = false;
};

std::shared_ptr<TextArea> TextArea::create() {
    return std::shared_ptr<TextArea>(new TextArea());
}

TextArea::TextArea() : impl_(std::make_unique<Impl>()) {
    set_focusable(true);
    add_style_class("text-area");
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::TextInput);
    accessible.set_value(impl_->text);
}

TextArea::~TextArea() = default;

std::string_view TextArea::text() const {
    return impl_->text;
}

void TextArea::set_text(std::string text) {
    if (impl_->text != text) {
        impl_->text = std::move(text);
        ensure_accessible().set_value(impl_->text);
        impl_->cursor_pos = impl_->text.size();
        impl_->scroll_offset = 0.0F;
        impl_->text_changed.emit();
        queue_layout();
        queue_redraw();
    }
}

std::string_view TextArea::placeholder() const {
    return impl_->placeholder;
}

void TextArea::set_placeholder(std::string placeholder) {
    if (impl_->placeholder != placeholder) {
        impl_->placeholder = std::move(placeholder);
        queue_redraw();
    }
}

bool TextArea::is_editable() const {
    return impl_->editable;
}

void TextArea::set_editable(bool editable) {
    if (impl_->editable != editable) {
        impl_->editable = editable;
        queue_redraw();
    }
}

int TextArea::visible_rows() const {
    return impl_->visible_rows;
}

void TextArea::set_visible_rows(int rows) {
    rows = std::max(1, rows);
    if (impl_->visible_rows != rows) {
        impl_->visible_rows = rows;
        queue_layout();
        queue_redraw();
    }
}

Signal<>& TextArea::on_text_changed() {
    return impl_->text_changed;
}

SizeRequest TextArea::measure(const Constraints& /*constraints*/) const {
    const float line_height = theme_number("line-height", 20.0F);
    const float min_width = theme_number("min-width", 300.0F);
    const float padding = theme_number("padding", 8.0F);
    const float h = static_cast<float>(impl_->visible_rows) * line_height + (padding * 2.0F);
    return {min_width, h, min_width, h};
}

bool TextArea::handle_mouse_event(const MouseEvent& event) {
    if (event.button != 1) {
        return false;
    }

    switch (event.type) {
    case MouseEvent::Type::Press:
        if (allocation().contains({event.x, event.y})) {
            grab_focus();
            return true;
        }
        return false;
    case MouseEvent::Type::Release:
    case MouseEvent::Type::Move:
    case MouseEvent::Type::Enter:
    case MouseEvent::Type::Leave:
    case MouseEvent::Type::Scroll:
        return false;
    }

    return false;
}

bool TextArea::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }

    if (!impl_->editable) {
        return false;
    }

    switch (event.key) {
    case KeyCode::Return: {
        impl_->text.insert(impl_->cursor_pos, 1, '\n');
        impl_->cursor_pos += 1;
        ensure_accessible().set_value(impl_->text);
        impl_->text_changed.emit();
        queue_redraw();
        return true;
    }
    case KeyCode::Backspace:
        if (impl_->cursor_pos > 0) {
            impl_->text.erase(impl_->cursor_pos - 1, 1);
            --impl_->cursor_pos;
            ensure_accessible().set_value(impl_->text);
            impl_->text_changed.emit();
            queue_redraw();
        }
        return true;
    case KeyCode::Delete:
        if (impl_->cursor_pos < impl_->text.size()) {
            impl_->text.erase(impl_->cursor_pos, 1);
            ensure_accessible().set_value(impl_->text);
            impl_->text_changed.emit();
            queue_redraw();
        }
        return true;
    case KeyCode::Left:
        if (impl_->cursor_pos > 0) {
            --impl_->cursor_pos;
            queue_redraw();
        }
        return true;
    case KeyCode::Right:
        if (impl_->cursor_pos < impl_->text.size()) {
            ++impl_->cursor_pos;
            queue_redraw();
        }
        return true;
    case KeyCode::Up: {
        // Move cursor up one line.
        const auto lines = split_lines(impl_->text);
        std::size_t pos = 0;
        std::size_t line_idx = 0;
        std::size_t col = 0;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (pos + lines[i].size() >= impl_->cursor_pos &&
                (i + 1 == lines.size() || pos + lines[i].size() + 1 > impl_->cursor_pos)) {
                line_idx = i;
                col = impl_->cursor_pos - pos;
                break;
            }
            pos += lines[i].size() + 1;
        }
        if (line_idx > 0) {
            std::size_t prev_start = 0;
            for (std::size_t i = 0; i < line_idx - 1; ++i) {
                prev_start += lines[i].size() + 1;
            }
            impl_->cursor_pos = prev_start + std::min(col, lines[line_idx - 1].size());
            queue_redraw();
        }
        return true;
    }
    case KeyCode::Down: {
        // Move cursor down one line.
        const auto lines = split_lines(impl_->text);
        std::size_t pos = 0;
        std::size_t line_idx = 0;
        std::size_t col = 0;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (pos + lines[i].size() >= impl_->cursor_pos &&
                (i + 1 == lines.size() || pos + lines[i].size() + 1 > impl_->cursor_pos)) {
                line_idx = i;
                col = impl_->cursor_pos - pos;
                break;
            }
            pos += lines[i].size() + 1;
        }
        if (line_idx + 1 < lines.size()) {
            std::size_t next_start = 0;
            for (std::size_t i = 0; i <= line_idx; ++i) {
                next_start += lines[i].size() + 1;
            }
            impl_->cursor_pos = next_start + std::min(col, lines[line_idx + 1].size());
            queue_redraw();
        }
        return true;
    }
    case KeyCode::Home:
        impl_->cursor_pos = 0;
        queue_redraw();
        return true;
    case KeyCode::End:
        impl_->cursor_pos = impl_->text.size();
        queue_redraw();
        return true;
    default:
        break;
    }

    return false;
}

bool TextArea::handle_text_input_event(const TextInputEvent& event) {
    if (!impl_->editable) {
        return false;
    }

    switch (event.type) {
    case TextInputEvent::Type::Commit:
        if (event.text.empty()) {
            return false;
        }
        impl_->text.insert(impl_->cursor_pos, event.text);
        impl_->cursor_pos += event.text.size();
        ensure_accessible().set_value(impl_->text);
        impl_->text_changed.emit();
        queue_redraw();
        return true;
    case TextInputEvent::Type::Preedit:
    case TextInputEvent::Type::ClearPreedit:
    case TextInputEvent::Type::DeleteSurrounding:
        return false;
    }

    return false;
}

CursorShape TextArea::cursor_shape() const {
    return impl_->editable ? CursorShape::IBeam : CursorShape::Default;
}

void TextArea::on_focus_changed(bool focused) {
    impl_->focused_state = focused;
    queue_redraw();
}

void TextArea::snapshot(SnapshotContext& ctx) const {
    const auto a = allocation();
    const float corner_radius = theme_number("corner-radius", 8.0F);
    const float padding = theme_number("padding", 8.0F);
    const float line_height = theme_number("line-height", 20.0F);
    const auto font = text_area_font();

    auto body = a;
    if (has_flag(state_flags(), StateFlags::Focused)) {
        ctx.add_rounded_rect(a,
                             theme_color("focus-ring-color", Color{0.3F, 0.56F, 0.9F, 1.0F}),
                             corner_radius + 2.0F);
        body = {a.x + 2.0F, a.y + 2.0F, a.width - 4.0F, a.height - 4.0F};
    }

    // Background and border.
    ctx.add_rounded_rect(
        body, theme_color("background", Color{1.0F, 1.0F, 1.0F, 1.0F}), corner_radius);
    ctx.add_border(
        body, theme_color("border-color", Color{0.8F, 0.82F, 0.86F, 1.0F}), 1.0F, corner_radius);

    const Rect text_area{body.x + padding, body.y + padding,
                         std::max(0.0F, body.width - padding * 2.0F),
                         std::max(0.0F, body.height - padding * 2.0F)};

    ctx.push_rounded_clip(body, corner_radius);

    if (impl_->text.empty() && !impl_->placeholder.empty()) {
        // Draw placeholder.
        const auto ph_color = theme_color("placeholder-color", Color{0.55F, 0.58F, 0.62F, 1.0F});
        ctx.add_text({text_area.x, text_area.y - impl_->scroll_offset},
                     std::string(impl_->placeholder),
                     ph_color,
                     font);
    } else {
        // Draw text lines.
        const auto lines = split_lines(impl_->text);
        const auto text_color = theme_color("text-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
        float y_offset = text_area.y - impl_->scroll_offset;
        for (const auto& line : lines) {
            if (y_offset + line_height > body.y && y_offset < body.bottom()) {
                ctx.add_text({text_area.x, y_offset},
                             std::string(line),
                             text_color,
                             font);
            }
            y_offset += line_height;
        }

        // Draw cursor if focused.
        if (impl_->focused_state) {
            const auto cursor_color = theme_color("caret-color", Color{0.1F, 0.1F, 0.1F, 1.0F});
            // Find cursor line and column.
            std::size_t pos = 0;
            std::size_t cursor_line = 0;
            std::size_t cursor_col = 0;
            for (std::size_t i = 0; i < lines.size(); ++i) {
                if (pos + lines[i].size() >= impl_->cursor_pos &&
                    (i + 1 == lines.size() ||
                     pos + lines[i].size() + 1 > impl_->cursor_pos)) {
                    cursor_line = i;
                    cursor_col = impl_->cursor_pos - pos;
                    break;
                }
                pos += lines[i].size() + 1;
            }
            const auto prefix = lines[cursor_line].substr(0, cursor_col);
            const float cursor_x = text_area.x + measure_text(prefix, font).width;
            const float cursor_y =
                text_area.y - impl_->scroll_offset +
                static_cast<float>(cursor_line) * line_height;
            ctx.add_color_rect({cursor_x, cursor_y + 2.0F, 1.5F, line_height - 4.0F},
                               cursor_color);
        }
    }

    ctx.pop_container();
}

} // namespace nk
