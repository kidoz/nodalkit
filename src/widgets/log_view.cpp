/// @file log_view.cpp
/// @brief Implementation of the append-only, virtualized LogView.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <nk/platform/events.h>
#include <nk/render/snapshot_context.h>
#include <nk/text/font.h>
#include <nk/widgets/log_view.h>

namespace nk {

namespace {

struct LogLine {
    std::string text;
    LogSeverity severity = LogSeverity::Normal;
};

constexpr float kFontSize = 12.5F;
constexpr float kLinePadding = 4.0F; // vertical padding folded into line height
constexpr float kHorizontalPadding = 8.0F;
constexpr std::size_t kDefaultMaxLines = 10000;

FontDescriptor log_font() {
    FontDescriptor font;
    font.family = "monospace";
    font.size = kFontSize;
    font.weight = FontWeight::Regular;
    return font;
}

std::string to_lower(std::string_view text) {
    std::string out(text);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

} // namespace

struct LogView::Impl {
    std::deque<LogLine> lines;
    std::size_t max_lines = kDefaultMaxLines;
    bool auto_scroll = true;
    float scroll_offset = 0.0F; // pixels scrolled down from the top of the content

    std::string query_lower;
    std::vector<std::size_t> matches;
    std::size_t current_match = 0; // index into `matches`

    Signal<> lines_changed;
};

std::shared_ptr<LogView> LogView::create() {
    return std::shared_ptr<LogView>(new LogView());
}

LogView::LogView() : impl_(std::make_unique<Impl>()) {
    add_style_class("log-view");
    set_focusable(true);
    auto& accessible = ensure_accessible();
    accessible.set_role(AccessibleRole::List);
    accessible.set_name("Log");
}

LogView::~LogView() = default;

float LogView::line_height() const {
    return std::round(kFontSize + kLinePadding * 2.0F);
}

float LogView::max_scroll() const {
    const float content = static_cast<float>(impl_->lines.size()) * line_height();
    return std::max(0.0F, content - allocation().height);
}

void LogView::clamp_scroll() {
    impl_->scroll_offset = std::clamp(impl_->scroll_offset, 0.0F, max_scroll());
}

void LogView::stick_to_bottom_if_following() {
    if (impl_->auto_scroll) {
        impl_->scroll_offset = max_scroll();
    }
}

void LogView::append_line(std::string_view text, LogSeverity severity) {
    std::string_view trimmed = text;
    if (!trimmed.empty() && trimmed.back() == '\n') {
        trimmed.remove_suffix(1);
    }
    impl_->lines.push_back({std::string(trimmed), severity});

    if (impl_->max_lines != 0 && impl_->lines.size() > impl_->max_lines) {
        const std::size_t drop = impl_->lines.size() - impl_->max_lines;
        impl_->lines.erase(impl_->lines.begin(),
                           impl_->lines.begin() + static_cast<std::ptrdiff_t>(drop));
        // Keep the viewport visually stable when older lines fall off the top.
        impl_->scroll_offset =
            std::max(0.0F, impl_->scroll_offset - static_cast<float>(drop) * line_height());
        // A previous search's indices no longer line up after trimming.
        if (!impl_->matches.empty()) {
            impl_->matches.clear();
            impl_->query_lower.clear();
        }
    }

    stick_to_bottom_if_following();
    clamp_scroll();
    impl_->lines_changed.emit();
    queue_redraw();
}

void LogView::clear() {
    impl_->lines.clear();
    impl_->scroll_offset = 0.0F;
    impl_->matches.clear();
    impl_->query_lower.clear();
    impl_->current_match = 0;
    impl_->lines_changed.emit();
    queue_redraw();
}

std::size_t LogView::line_count() const {
    return impl_->lines.size();
}

std::string_view LogView::line_text(std::size_t index) const {
    if (index >= impl_->lines.size()) {
        return {};
    }
    return impl_->lines[index].text;
}

LogSeverity LogView::line_severity(std::size_t index) const {
    if (index >= impl_->lines.size()) {
        return LogSeverity::Normal;
    }
    return impl_->lines[index].severity;
}

void LogView::set_max_lines(std::size_t max_lines) {
    impl_->max_lines = max_lines;
    if (max_lines != 0 && impl_->lines.size() > max_lines) {
        const std::size_t drop = impl_->lines.size() - max_lines;
        impl_->lines.erase(impl_->lines.begin(),
                           impl_->lines.begin() + static_cast<std::ptrdiff_t>(drop));
        impl_->matches.clear();
        impl_->query_lower.clear();
        clamp_scroll();
        impl_->lines_changed.emit();
        queue_redraw();
    }
}

std::size_t LogView::max_lines() const {
    return impl_->max_lines;
}

void LogView::set_auto_scroll(bool enabled) {
    impl_->auto_scroll = enabled;
    if (enabled) {
        stick_to_bottom_if_following();
        queue_redraw();
    }
}

bool LogView::auto_scroll() const {
    return impl_->auto_scroll;
}

std::vector<std::size_t> LogView::search(std::string_view query) {
    impl_->matches.clear();
    impl_->current_match = 0;
    impl_->query_lower = to_lower(query);

    if (!impl_->query_lower.empty()) {
        for (std::size_t i = 0; i < impl_->lines.size(); ++i) {
            if (to_lower(impl_->lines[i].text).find(impl_->query_lower) != std::string::npos) {
                impl_->matches.push_back(i);
            }
        }
    }

    if (!impl_->matches.empty()) {
        scroll_to_line(impl_->matches.front());
    }
    queue_redraw();
    return impl_->matches;
}

std::span<const std::size_t> LogView::matches() const {
    return impl_->matches;
}

void LogView::next_match() {
    if (impl_->matches.empty()) {
        return;
    }
    impl_->current_match = (impl_->current_match + 1) % impl_->matches.size();
    scroll_to_line(impl_->matches[impl_->current_match]);
    queue_redraw();
}

void LogView::previous_match() {
    if (impl_->matches.empty()) {
        return;
    }
    impl_->current_match =
        (impl_->current_match + impl_->matches.size() - 1) % impl_->matches.size();
    scroll_to_line(impl_->matches[impl_->current_match]);
    queue_redraw();
}

void LogView::scroll_to_line(std::size_t index) {
    // Jumping to a specific line means the user is no longer following the tail.
    impl_->auto_scroll = false;
    const float target = static_cast<float>(index) * line_height();
    // Center the line in the viewport when possible.
    impl_->scroll_offset = target - allocation().height * 0.5F + line_height() * 0.5F;
    clamp_scroll();
}

std::string LogView::export_text() const {
    std::string out;
    for (std::size_t i = 0; i < impl_->lines.size(); ++i) {
        if (i != 0) {
            out.push_back('\n');
        }
        out += impl_->lines[i].text;
    }
    return out;
}

Signal<>& LogView::on_lines_changed() {
    return impl_->lines_changed;
}

SizeRequest LogView::measure(const Constraints& constraints) const {
    // The log fills whatever space it is given; it does not drive layout by its
    // (potentially enormous) content height. Report a modest minimum and let the
    // parent expand it.
    const float min_height = line_height() * 3.0F;
    const float natural_width =
        std::isfinite(constraints.max_width) ? constraints.max_width : 320.0F;
    return {120.0F, min_height, natural_width, line_height() * 12.0F};
}

void LogView::allocate(const Rect& allocation) {
    Widget::allocate(allocation);
    stick_to_bottom_if_following();
    clamp_scroll();
}

bool LogView::handle_mouse_event(const MouseEvent& event) {
    if (event.type != MouseEvent::Type::Scroll) {
        return false;
    }

    const float step = event.precise_scrolling ? 1.0F : line_height() * 3.0F;
    impl_->scroll_offset -= event.scroll_dy * step;
    clamp_scroll();

    // Following the tail resumes only when the user is back at the bottom.
    impl_->auto_scroll = impl_->scroll_offset >= max_scroll() - 0.5F;

    queue_redraw();
    return true;
}

bool LogView::handle_key_event(const KeyEvent& event) {
    if (event.type != KeyEvent::Type::Press) {
        return false;
    }
    switch (event.key) {
    case KeyCode::PageUp:
        impl_->scroll_offset -= allocation().height;
        impl_->auto_scroll = false;
        clamp_scroll();
        queue_redraw();
        return true;
    case KeyCode::PageDown:
        impl_->scroll_offset += allocation().height;
        clamp_scroll();
        impl_->auto_scroll = impl_->scroll_offset >= max_scroll() - 0.5F;
        queue_redraw();
        return true;
    case KeyCode::End:
        impl_->auto_scroll = true;
        impl_->scroll_offset = max_scroll();
        queue_redraw();
        return true;
    case KeyCode::Home:
        impl_->auto_scroll = false;
        impl_->scroll_offset = 0.0F;
        queue_redraw();
        return true;
    default:
        return false;
    }
}

void LogView::snapshot(SnapshotContext& ctx) const {
    const Rect a = allocation();
    if (a.width <= 0.0F || a.height <= 0.0F) {
        return;
    }

    const Color background = theme_color("view-background", Color::from_rgb(24, 24, 28));
    ctx.add_color_rect(a, background);

    const FontDescriptor font = log_font();
    const float lh = line_height();

    auto severity_color = [&](LogSeverity severity) -> Color {
        switch (severity) {
        case LogSeverity::Info:
            return theme_color("accent-color", Color::from_rgb(120, 170, 255));
        case LogSeverity::Warning:
            return Color::from_rgb(230, 180, 80);
        case LogSeverity::Error:
            return Color::from_rgb(240, 110, 100);
        case LogSeverity::Success:
            return Color::from_rgb(130, 200, 130);
        case LogSeverity::Normal:
        default:
            return theme_color("text-color", Color::from_rgb(220, 220, 220));
        }
    };

    const std::size_t total = impl_->lines.size();
    const std::size_t first = static_cast<std::size_t>(std::floor(impl_->scroll_offset / lh));
    const std::size_t visible = static_cast<std::size_t>(std::ceil(a.height / lh)) + 1;
    const std::size_t last = std::min(total, first + visible);

    // Highlight tint for lines matched by the current search.
    const Color match_tint = Color::from_rgb(70, 90, 40);
    const Color current_tint = Color::from_rgb(110, 140, 60);

    ctx.push_rounded_clip(a, 0.0F);
    for (std::size_t i = first; i < last; ++i) {
        const float y = a.y - impl_->scroll_offset + static_cast<float>(i) * lh;
        const LogLine& line = impl_->lines[i];

        if (!impl_->matches.empty()) {
            const auto it = std::lower_bound(impl_->matches.begin(), impl_->matches.end(), i);
            if (it != impl_->matches.end() && *it == i) {
                const bool is_current =
                    !impl_->matches.empty() && impl_->matches[impl_->current_match] == i;
                ctx.add_color_rect({a.x, y, a.width, lh}, is_current ? current_tint : match_tint);
            }
        }

        const float text_y = y + kLinePadding;
        add_text_elided(ctx,
                        {a.x + kHorizontalPadding, text_y},
                        line.text,
                        a.width - kHorizontalPadding * 2.0F,
                        severity_color(line.severity),
                        font);
    }
    ctx.pop_container();
}

} // namespace nk
