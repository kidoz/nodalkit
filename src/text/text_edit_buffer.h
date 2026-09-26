#pragma once

#include <cstddef>
#include <nk/platform/events.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nk::detail {

enum class EditGroup { None, Insert, DeleteBackward, DeleteForward };

// Private shared document state. Widgets supply valid character-boundary offsets
// and own layout, input policy, clipboard ownership, and composition presentation.
class TextEditBuffer {
public:
    std::string text;
    std::size_t cursor = 0;
    std::size_t selection_anchor = 0;
    std::string preedit_text;
    std::size_t preedit_selection_start = 0;
    std::size_t preedit_selection_end = 0;

    [[nodiscard]] std::size_t selection_start() const;
    [[nodiscard]] std::size_t selection_end() const;
    [[nodiscard]] bool has_selection() const;
    void move_cursor(std::size_t position, bool extend);
    void select_all();
    // Clamp an input-method byte range to the text and widen it to whole code points.
    [[nodiscard]] TextInputRange code_point_range(TextInputRange range) const;
    // Compose over `range` instead of the selection. Clearing the composition
    // restores the previous caret and selection.
    void set_preedit_target(TextInputRange range);
    bool replace(std::size_t start,
                 std::size_t end,
                 std::string_view inserted,
                 EditGroup group = EditGroup::None);
    bool undo();
    bool redo();
    void reset_history();
    void break_undo_group();
    void set_preedit(std::string text, std::size_t start, std::size_t end);
    bool clear_preedit();
    [[nodiscard]] bool has_preedit() const;
    [[nodiscard]] std::string display_text() const;
    [[nodiscard]] std::size_t display_caret_position() const;
    [[nodiscard]] std::size_t text_position_from_display(std::size_t position) const;
    bool delete_surrounding(std::size_t before, std::size_t after);

private:
    struct State {
        std::string text;
        std::size_t cursor = 0;
        std::size_t anchor = 0;
    };

    [[nodiscard]] State state() const;
    void restore(const State& state);
    std::vector<State> history_{{}};
    std::size_t history_index_ = 0;
    EditGroup last_group_ = EditGroup::None;

    struct PreeditOrigin {
        std::size_t cursor = 0;
        std::size_t anchor = 0;
        std::size_t target_cursor = 0;
        std::size_t target_anchor = 0;
        std::size_t text_size = 0;
    };

    std::optional<PreeditOrigin> preedit_origin_;
};

} // namespace nk::detail
