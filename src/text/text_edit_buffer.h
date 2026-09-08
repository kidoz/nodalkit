#pragma once

#include <cstddef>
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

    [[nodiscard]] std::size_t selection_start() const;
    [[nodiscard]] std::size_t selection_end() const;
    [[nodiscard]] bool has_selection() const;
    void move_cursor(std::size_t position, bool extend);
    void select_all();
    bool replace(std::size_t start,
                 std::size_t end,
                 std::string_view inserted,
                 EditGroup group = EditGroup::None);
    bool undo();
    bool redo();
    void reset_history();
    void break_undo_group();

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
};

} // namespace nk::detail
