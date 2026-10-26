#pragma once

/// @file command_palette.h
/// @brief Searchable command picker widget.

#include <cstddef>
#include <memory>
#include <nk/actions/action.h>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// User-visible command entry displayed by CommandPalette.
struct CommandPaletteCommand {
    std::string id;
    std::string title;
    std::string subtitle;
    std::string category;
    bool enabled = true;
    bool checked = false;
};

/// In-window searchable command palette with keyboard and pointer activation.
class CommandPalette : public Widget {
public:
    /// Creates a CommandPalette owned by a shared pointer.
    [[nodiscard]] static std::shared_ptr<CommandPalette> create();
    ~CommandPalette() override;

    /// Replaces all commands and rebuilds the active filter.
    void set_commands(std::vector<CommandPaletteCommand> commands);
    /// Returns all source commands in insertion order.
    [[nodiscard]] std::span<const CommandPaletteCommand> commands() const;

    /// Bind command IDs to a registry. Effective enabled/checked state is
    /// reflected in commands(); missing actions are disabled. Null unbinds.
    void set_action_group(std::shared_ptr<ActionGroup> actions);
    [[nodiscard]] ActionGroup* action_group() const;

    /// Sets the search query.
    void set_query(std::string query);
    /// Returns the current search query.
    [[nodiscard]] std::string_view query() const;
    /// Clears the current search query.
    void clear_query();

    /// Returns the source command index for the current result.
    [[nodiscard]] std::optional<std::size_t> current_command() const;

    /// Emitted for enabled commands when no action group is bound. Bound
    /// palettes activate the shared action instead of emitting this signal.
    Signal<std::string_view>& on_command_activated();

    /// Emitted when Escape is pressed with an empty query.
    Signal<>& on_dismiss_requested();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    bool handle_text_input_event(const TextInputEvent& event) override;
    [[nodiscard]] std::optional<WidgetTextInputState> text_input_state() const override;
    [[nodiscard]] CursorShape cursor_shape() const override;
    void on_focus_changed(bool focused) override;

protected:
    CommandPalette();
    void snapshot(SnapshotContext& ctx) const override;

private:
    void sync_actions();
    void rebuild_filter();
    void sync_accessible_summary();
    void move_current(int delta);
    void set_current_result(std::optional<std::size_t> result);
    void ensure_current_visible();
    [[nodiscard]] bool activate_current();
    [[nodiscard]] std::optional<std::size_t> result_at_point(Point point) const;
    [[nodiscard]] float resolved_row_height() const;
    [[nodiscard]] float resolved_search_height() const;
    [[nodiscard]] float resolved_search_section_height() const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
