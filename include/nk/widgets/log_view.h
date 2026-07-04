#pragma once

/// @file log_view.h
/// @brief Append-only, virtualized log view for high-volume streaming text.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// Severity of a log line, used to pick its text color.
enum class LogSeverity : std::uint8_t {
    Normal,
    Info,
    Warning,
    Error,
    Success,
};

/// An append-only, virtualized log view built for high-volume streaming output
/// such as an emulator's HLE/debug trace.
///
/// Appending one line is O(1) and never rebuilds the whole buffer — unlike
/// TextArea, which replaces its entire string on every change. Only the lines
/// currently in view are rendered, so a log of many thousands of lines scrolls
/// without UI stalls. A retention cap keeps memory bounded by dropping the
/// oldest lines (a ring buffer).
///
/// Each line carries a LogSeverity used for color styling. The view sticks to
/// the newest line on append unless auto-scroll is paused, which lets the user
/// scroll back to read while output keeps arriving. A case-insensitive search
/// highlights matches and can step through them.
///
/// Thread affinity: like every widget, drive it on the UI/event-loop thread.
/// Marshal worker-thread output with EventLoop::post() before calling
/// append_line().
class LogView : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<LogView> create();
    ~LogView() override;

    /// Append one line. A single trailing '\n' in @p text is ignored; embedded
    /// newlines are kept as-is (the line is still one logical entry).
    void append_line(std::string_view text, LogSeverity severity = LogSeverity::Normal);

    /// Remove all lines and clear any active search.
    void clear();

    /// Number of retained lines.
    [[nodiscard]] std::size_t line_count() const;

    /// The text and severity of a retained line, by index. Returns an empty
    /// string for out-of-range indices.
    [[nodiscard]] std::string_view line_text(std::size_t index) const;
    [[nodiscard]] LogSeverity line_severity(std::size_t index) const;

    /// Cap on retained lines; older lines are dropped once the cap is exceeded.
    /// 0 means unbounded. Default is 10000.
    void set_max_lines(std::size_t max_lines);
    [[nodiscard]] std::size_t max_lines() const;

    /// Whether the view snaps to the newest line on append. Set false to
    /// "pause" auto-scroll so the user can read back; true snaps to the end and
    /// resumes following. Scrolling up by hand pauses it automatically;
    /// scrolling back to the bottom resumes it.
    void set_auto_scroll(bool enabled);
    [[nodiscard]] bool auto_scroll() const;

    /// Case-insensitive substring search. Returns the 0-based indices of all
    /// matching lines, records them for highlighting, and scrolls the first
    /// match into view. An empty query clears the highlight and returns empty.
    std::vector<std::size_t> search(std::string_view query);

    /// The indices matched by the most recent search().
    [[nodiscard]] std::span<const std::size_t> matches() const;

    /// Step the current match forward/backward, scrolling it into view. No-ops
    /// when there are no matches.
    void next_match();
    void previous_match();

    /// All retained lines joined with '\n', for copy or export. Applications
    /// hand the result to Application clipboard/save APIs.
    [[nodiscard]] std::string export_text() const;

    /// Emitted after the set of lines changes (append, clear, or trim).
    Signal<>& on_lines_changed();

    // --- Widget overrides ---
    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;

protected:
    LogView();
    void snapshot(SnapshotContext& ctx) const override;

private:
    [[nodiscard]] float line_height() const;
    [[nodiscard]] float max_scroll() const;
    void clamp_scroll();
    void scroll_to_line(std::size_t index);
    void stick_to_bottom_if_following();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
