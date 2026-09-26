#include "../src/platform/macos/text_input_routing.h"
#include "../src/text/native_input_document.h"
#include "../src/text/text_boundaries.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/render/render_node.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/search_field.h>
#include <nk/widgets/text_area.h>
#include <nk/widgets/text_field.h>
#include <string>
#include <utility>
#include <vector>

namespace {
bool key(nk::Widget& widget, nk::KeyCode code, nk::Modifiers modifiers = nk::Modifiers::None) {
    return widget.handle_key_event(
        {.type = nk::KeyEvent::Type::Press, .key = code, .modifiers = modifiers});
}

bool commit(nk::Widget& widget, std::string text) {
    return widget.handle_text_input_event(
        {.type = nk::TextInputEvent::Type::Commit, .text = std::move(text)});
}

std::vector<std::string> painted_text(nk::Widget& widget) {
    nk::SnapshotContext context;
    widget.snapshot(context);
    const auto root = context.take_root();
    std::vector<std::string> result;
    const auto collect = [&](const auto& self, const nk::RenderNode& node) -> void {
        if (node.kind() == nk::RenderNodeKind::Text) {
            result.push_back(static_cast<const nk::TextNode&>(node).text());
        }
        for (const auto& child : node.children()) {
            self(self, *child);
        }
    };
    collect(collect, *root);
    return result;
}
} // namespace

TEST_CASE("TextArea replaces a keyboard selection and undoes it", "[text_area][editing]") {
    auto area = nk::TextArea::create();
    area->set_text("first\ncaf\u00E9");
    REQUIRE(key(*area, nk::KeyCode::Home, nk::Modifiers::Shift));
    REQUIRE(commit(*area, "last"));
    CHECK(area->text() == "first\nlast");
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(area->text() == "first\ncaf\u00E9");
}

TEST_CASE("TextArea and TextField share clipboard contents", "[text_area][clipboard]") {
    auto area = nk::TextArea::create();
    area->set_text("first\n\u754C");
    REQUIRE(key(*area, nk::KeyCode::A, nk::Modifiers::Ctrl));
    REQUIRE(key(*area, nk::KeyCode::C, nk::Modifiers::Ctrl));
    auto field = nk::TextField::create();
    REQUIRE(key(*field, nk::KeyCode::V, nk::Modifiers::Ctrl));
    CHECK(field->text() == area->text());
    field->set_text("replacement");
    field->select_all();
    REQUIRE(key(*field, nk::KeyCode::C, nk::Modifiers::Super));
    REQUIRE(key(*area, nk::KeyCode::V, nk::Modifiers::Super));
    CHECK(area->text() == "replacement");
}

TEST_CASE("TextArea replaces a pointer selection across lines", "[text_area][selection]") {
    auto area = nk::TextArea::create();
    area->allocate({0, 0, 200, 100});
    area->set_text("first\nsecond\nthird");
    REQUIRE(area->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 8, .y = 10, .button = 1}));
    REQUIRE(area->handle_mouse_event({.type = nk::MouseEvent::Type::Move, .x = 8, .y = 50}));
    REQUIRE(area->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 8, .y = 50, .button = 1}));
    REQUIRE(commit(*area, "|"));
    CHECK(area->text() == "|third");
}

TEST_CASE("TextArea groups consecutive typing into one undo step", "[text_area][history]") {
    auto area = nk::TextArea::create();
    REQUIRE(commit(*area, "a"));
    REQUIRE(commit(*area, "b"));
    REQUIRE(commit(*area, "c"));
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(area->text().empty());
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Super | nk::Modifiers::Shift));
    CHECK(area->text() == "abc");
}

TEST_CASE("Shared editor history separates edit kinds and discards stale redo", "[text][history]") {
    auto check = [](auto editor) {
        REQUIRE(commit(*editor, "a"));
        REQUIRE(commit(*editor, "b"));
        REQUIRE(key(*editor, nk::KeyCode::Backspace));
        CHECK(editor->text() == "a");
        REQUIRE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(editor->text() == "ab");
        REQUIRE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(editor->text().empty());
        REQUIRE(key(*editor, nk::KeyCode::Y, nk::Modifiers::Ctrl));
        CHECK(editor->text() == "ab");
        REQUIRE(key(*editor, nk::KeyCode::Left));
        REQUIRE(commit(*editor, "X"));
        CHECK(editor->text() == "aXb");
        CHECK_FALSE(key(*editor, nk::KeyCode::Y, nk::Modifiers::Ctrl));
        REQUIRE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(editor->text() == "ab");
        CHECK(editor->cursor_position() == 1);
    };
    check(nk::TextArea::create());
    check(nk::TextField::create());
}

TEST_CASE("Shared editor undo restores the selection before replacement", "[text][history]") {
    auto check = [](auto editor) {
        editor->set_text("caf\u00E9");
        editor->select_all();
        REQUIRE(commit(*editor, "x"));
        REQUIRE(commit(*editor, "y"));
        REQUIRE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(editor->text() == "x");
        REQUIRE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(editor->text() == "caf\u00E9");
        CHECK(editor->selection_start() == 0);
        CHECK(editor->selection_end() == editor->text().size());
        REQUIRE(key(*editor, nk::KeyCode::Right));
        CHECK_FALSE(editor->has_selection());
        CHECK(editor->cursor_position() == editor->text().size());
        editor->set_text("new document");
        CHECK_FALSE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    };
    check(nk::TextArea::create());
    check(nk::TextField::create());
}

TEST_CASE("TextArea read-only mode allows selection and copy but rejects history and edits",
          "[text_area][readonly]") {
    auto area = nk::TextArea::create();
    area->set_text("one\n\u754C");
    REQUIRE(commit(*area, "!"));
    area->set_editable(false);
    area->select_all();
    CHECK(area->has_selection());
    int changes = 0;
    auto changed = area->on_text_changed().connect([&] { ++changes; });
    REQUIRE(key(*area, nk::KeyCode::C, nk::Modifiers::Ctrl));
    for (auto code : {nk::KeyCode::X, nk::KeyCode::V, nk::KeyCode::Z, nk::KeyCode::Y}) {
        CHECK_FALSE(key(*area, code, nk::Modifiers::Ctrl));
    }
    for (auto code : {nk::KeyCode::Backspace, nk::KeyCode::Delete, nk::KeyCode::Return}) {
        CHECK_FALSE(key(*area, code));
    }
    CHECK_FALSE(commit(*area, "blocked"));
    CHECK(changes == 0);
    auto sink = nk::TextField::create();
    REQUIRE(key(*sink, nk::KeyCode::V, nk::Modifiers::Ctrl));
    CHECK(sink->text() == "one\n\u754C!");
    area->set_editable(true);
    REQUIRE(key(*area, nk::KeyCode::X, nk::Modifiers::Ctrl));
    CHECK(area->text().empty());
    CHECK(changes == 1);
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(area->text() == "one\n\u754C!");
    CHECK(changes == 2);
    CHECK(changed.connected());
}

TEST_CASE("TextArea shift movement selects newlines and complete clusters",
          "[text_area][selection]") {
    auto area = nk::TextArea::create();
    area->set_text("a\ne\u0301");
    REQUIRE(key(*area, nk::KeyCode::Left, nk::Modifiers::Shift));
    CHECK(area->selection_start() == 2);
    CHECK(area->selection_end() == 5);
    REQUIRE(key(*area, nk::KeyCode::Left, nk::Modifiers::Shift));
    CHECK(area->selection_start() == 1);
    REQUIRE(key(*area, nk::KeyCode::Backspace));
    CHECK(area->text() == "a");
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(area->text() == "a\ne\u0301");
    CHECK(area->cursor_position() == 1);
    CHECK(area->selection_end() == 5);
    REQUIRE(key(*area, nk::KeyCode::Left));
    CHECK_FALSE(area->has_selection());
    CHECK(area->cursor_position() == 1);
}

TEST_CASE("TextArea double and triple clicks select words and full hard lines",
          "[text_area][selection]") {
    auto area = nk::TextArea::create();
    area->allocate({0, 0, 240, 100});
    area->set_text("alpha beta\nsecond\nthird");
    REQUIRE(area->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 18, .y = 10, .button = 1, .click_count = 2}));
    CHECK(area->selection_start() == 0);
    CHECK(area->selection_end() == 5);
    REQUIRE(area->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 18, .y = 10, .button = 1}));
    REQUIRE(area->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 18, .y = 30, .button = 1, .click_count = 3}));
    CHECK(area->selection_start() == 11);
    CHECK(area->selection_end() == 18);
    REQUIRE(commit(*area, "replacement\n"));
    CHECK(area->text() == "alpha beta\nreplacement\nthird");
}

TEST_CASE("Window preserves TextArea drag selection and reveals the dragged caret",
          "[text_area][window]") {
    nk::Window window({.title = "Multiline selection", .width = 240, .height = 100});
    auto area = nk::TextArea::create();
    window.set_child(area);
    area->allocate({0, 0, 240, 56});
    area->set_text("first\nsecond\nthird\nfourth\nfifth");
    REQUIRE(key(*area, nk::KeyCode::Home, nk::Modifiers::Ctrl));
    window.dispatch_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 8, .y = 10, .button = 1});
    window.dispatch_mouse_event({.type = nk::MouseEvent::Type::Move, .x = 500, .y = 150});
    CHECK(area->selection_start() == 0);
    CHECK(area->selection_end() == area->text().size());
    CHECK(area->text_input_caret_rect().bottom() <= 48);
    window.dispatch_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 500, .y = 150, .button = 1});
    window.dispatch_text_input_event({.type = nk::TextInputEvent::Type::Commit, .text = "done"});
    CHECK(area->text() == "done");
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press,
                               .key = nk::KeyCode::Z,
                               .modifiers = nk::Modifiers::Ctrl});
    CHECK(area->text() == "first\nsecond\nthird\nfourth\nfifth");
    area->set_sensitive(false);
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Backspace});
    CHECK(area->text() == "first\nsecond\nthird\nfourth\nfifth");
}

TEST_CASE("TextArea selection painting stays in the padded viewport", "[text_area][render]") {
    auto area = nk::TextArea::create();
    area->allocate({0, 0, 160, 56});
    area->set_text(std::string(100, 'w') + "\n\nlast");
    const auto pixels = [&] {
        nk::SnapshotContext context;
        static_cast<nk::Widget&>(*area).snapshot(context);
        const auto root = context.take_root();
        nk::SoftwareRenderer renderer;
        renderer.begin_frame({160, 56}, 1.0F);
        renderer.render(*root);
        renderer.end_frame();
        return std::vector<uint8_t>(renderer.pixel_data(), renderer.pixel_data() + 160 * 56 * 4);
    };
    const auto before = pixels();
    area->select_all();
    const auto selected = pixels();
    bool changed_inside = false;
    for (int y = 0; y < 56; ++y) {
        for (int x = 0; x < 160; ++x) {
            const auto i = static_cast<std::size_t>((y * 160 + x) * 4);
            const bool same =
                std::equal(before.data() + i, before.data() + i + 4, selected.data() + i);
            if (x < 8 || x >= 152 || y < 8 || y >= 48) {
                REQUIRE(same);
            } else {
                changed_inside = changed_inside || !same;
            }
        }
    }
    CHECK(changed_inside);
}

TEST_CASE("TextArea inserts the shared primary selection at a middle click",
          "[text_area][clipboard]") {
    auto area = nk::TextArea::create();
    area->allocate({0, 0, 160, 56});
    area->set_text("target");
    auto field = nk::TextField::create("caf\u00E9");
    field->select_all();
    REQUIRE(area->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 8, .y = 10, .button = 3}));
    CHECK(area->text() == "caf\u00E9target");
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(area->text() == "target");
}

TEST_CASE("TextArea makes newline and paste separate undo steps", "[text_area][history]") {
    auto area = nk::TextArea::create();
    REQUIRE(commit(*area, "ab"));
    REQUIRE(key(*area, nk::KeyCode::Return));
    REQUIRE(commit(*area, "cd"));
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(area->text() == "ab\n");
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(area->text() == "ab");
    auto source = nk::TextField::create("paste");
    source->select_all();
    REQUIRE(key(*source, nk::KeyCode::C, nk::Modifiers::Ctrl));
    REQUIRE(key(*area, nk::KeyCode::V, nk::Modifiers::Ctrl));
    REQUIRE(commit(*area, "x"));
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(area->text() == "abpaste");
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(area->text() == "ab");
}

TEST_CASE("TextArea shift click extends selection and focus loss cancels dragging",
          "[text_area][selection]") {
    auto area = nk::TextArea::create();
    area->allocate({0, 0, 200, 100});
    area->set_text("first\nsecond\nthird");
    REQUIRE(key(*area, nk::KeyCode::Home, nk::Modifiers::Ctrl));
    REQUIRE(area->handle_mouse_event({.type = nk::MouseEvent::Type::Press,
                                      .x = 8,
                                      .y = 30,
                                      .button = 1,
                                      .modifiers = nk::Modifiers::Shift}));
    CHECK(area->selection_start() == 0);
    CHECK(area->selection_end() == 6);
    area->on_focus_changed(false);
    CHECK_FALSE(area->handle_mouse_event({.type = nk::MouseEvent::Type::Move, .x = 8, .y = 50}));
    CHECK(area->selection_end() == 6);
}

TEST_CASE("Window sends captured motion to a single-line editor outside its bounds",
          "[text][window]") {
    nk::Window window({.title = "Captured selection", .width = 240, .height = 100});
    auto field = nk::TextField::create("first second");
    window.set_child(field);
    field->allocate({0, 0, 240, 36});
    window.dispatch_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 12, .y = 18, .button = 1});
    window.dispatch_mouse_event({.type = nk::MouseEvent::Type::Move, .x = 500, .y = 150});
    CHECK(field->selection_start() == 0);
    CHECK(field->selection_end() == field->text().size());
    window.dispatch_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 500, .y = 150, .button = 1});
    REQUIRE(commit(*field, "done"));
    CHECK(field->text() == "done");
}

TEST_CASE("TextArea composition stays separate until one undoable commit", "[text_area][ime]") {
    auto area = nk::TextArea::create();
    area->allocate({0, 0, 220, 100});
    area->set_text("first\nold");
    REQUIRE(key(*area, nk::KeyCode::Home, nk::Modifiers::Shift));
    int changes = 0;
    auto connection = area->on_text_changed().connect([&] { ++changes; });
    REQUIRE(area->handle_text_input_event(
        {.type = nk::TextInputEvent::Type::Preedit, .text = "\u5019\u88DC", .selection_end = 6}));
    CHECK(area->text() == "first\nold");
    CHECK(changes == 0);
    REQUIRE(commit(*area, "\u88DC"));
    CHECK(area->text() == "first\n\u88DC");
    CHECK(changes == 1);
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(area->text() == "first\nold");
    CHECK(area->selection_start() == 6);
    CHECK(area->selection_end() == 9);
    CHECK(connection.connected());
}

TEST_CASE("TextArea cancels composition without editing the query", "[text_area][ime]") {
    auto area = nk::TextArea::create();
    area->set_text("kept");
    REQUIRE(area->handle_text_input_event(
        {.type = nk::TextInputEvent::Type::Preedit, .text = "pending"}));
    REQUIRE(key(*area, nk::KeyCode::Escape));
    CHECK(area->text() == "kept");
    CHECK_FALSE(key(*area, nk::KeyCode::Escape));
    CHECK_FALSE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
}

TEST_CASE("TextArea surrounding deletion is safe across Unicode and hard newlines",
          "[text_area][ime]") {
    auto area = nk::TextArea::create();
    area->set_text("a\ne\u0301");
    REQUIRE(area->handle_text_input_event({.type = nk::TextInputEvent::Type::DeleteSurrounding,
                                           .text = {},
                                           .delete_before_length = 1}));
    CHECK(area->text() == "a\n");
    REQUIRE(key(*area, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(area->text() == "a\ne\u0301");
}

TEST_CASE("Window exposes TextArea composition caret and committed surrounding state",
          "[text_area][ime]") {
    nk::Window window({.title = "Multiline composition", .width = 240, .height = 100});
    auto area = nk::TextArea::create();
    window.set_child(area);
    area->allocate({10, 10, 220, 56});
    area->set_text("a\nb");
    area->grab_focus();
    auto state = window.current_text_input_state();
    REQUIRE(state.has_value());
    CHECK(state->text == "a\nb");
    CHECK(state->cursor == 3);
    window.dispatch_text_input_event({.type = nk::TextInputEvent::Type::Preedit,
                                      .text = "\u754C\n" + std::string(100, 'w'),
                                      .selection_end = 104});
    state = window.current_text_input_state();
    REQUIRE(state.has_value());
    CHECK(state->text == "a\nb");
    CHECK(state->cursor == 3);
    CHECK(state->caret_rect.x >= 18);
    CHECK(state->caret_rect.right() <= 222.001F);
    CHECK(state->caret_rect.y >= 18);
    CHECK(state->caret_rect.bottom() <= 58.001F);
}

TEST_CASE("Editors paint preedit in place of a reversed selection", "[text][ime][render]") {
    const auto check = [](auto editor) {
        editor->allocate({0, 0, 240, 100});
        editor->set_text("old");
        REQUIRE(key(*editor, nk::KeyCode::Home, nk::Modifiers::Shift));
        int changes = 0;
        auto connection = editor->on_text_changed().connect([&](auto&&...) { ++changes; });
        REQUIRE(editor->handle_text_input_event({.type = nk::TextInputEvent::Type::Preedit,
                                                 .text = "\u5019\u88DC",
                                                 .selection_start = 3,
                                                 .selection_end = 6}));
        CHECK(painted_text(*editor) == std::vector<std::string>{"\u5019\u88DC"});
        CHECK(editor->text() == "old");
        const auto state = editor->text_input_state();
        REQUIRE(state.has_value());
        CHECK(state->text == "old");
        CHECK(state->cursor == 0);
        CHECK(state->anchor == 3);
        CHECK(changes == 0);
        REQUIRE(commit(*editor, "x"));
        REQUIRE(commit(*editor, "y"));
        REQUIRE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(editor->text() == "x");
        REQUIRE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(editor->text() == "old");
        CHECK(editor->cursor_position() == 0);
        CHECK(editor->selection_end() == 3);
        CHECK(connection.connected());
    };
    check(nk::TextArea::create());
    check(nk::TextField::create());
    check(nk::SearchField::create());
}

TEST_CASE("Editors cancel preedit on read-only transitions and empty commits", "[text][ime]") {
    const auto check = [](auto editor) {
        editor->allocate({0, 0, 240, 100});
        editor->set_text("kept");
        REQUIRE(editor->handle_text_input_event(
            {.type = nk::TextInputEvent::Type::Preedit, .text = "pending"}));
        REQUIRE(commit(*editor, ""));
        CHECK(painted_text(*editor) == std::vector<std::string>{"kept"});
        REQUIRE(editor->handle_text_input_event(
            {.type = nk::TextInputEvent::Type::Preedit, .text = "pending"}));
        editor->set_editable(false);
        CHECK_FALSE(editor->text_input_state().has_value());
        CHECK_FALSE(editor->handle_text_input_event(
            {.type = nk::TextInputEvent::Type::Preedit, .text = "blocked"}));
        CHECK_FALSE(
            editor->handle_text_input_event({.type = nk::TextInputEvent::Type::DeleteSurrounding,
                                             .text = {},
                                             .delete_before_length = 1}));
        CHECK(painted_text(*editor) == std::vector<std::string>{"kept"});
        editor->set_editable(true);
        REQUIRE(editor->handle_text_input_event(
            {.type = nk::TextInputEvent::Type::Preedit, .text = "pending"}));
        editor->on_focus_changed(false);
        CHECK(painted_text(*editor) == std::vector<std::string>{"kept"});
        CHECK_FALSE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    };
    check(nk::TextArea::create());
    check(nk::TextField::create());
}

TEST_CASE("Editors bound partial UTF-8 preedit offsets and surrounding deletion", "[text][ime]") {
    const auto check = [](auto editor) {
        editor->allocate({0, 0, 240, 100});
        editor->set_text("a");
        REQUIRE(editor->handle_text_input_event({.type = nk::TextInputEvent::Type::Preedit,
                                                 .text = "\u00E9x",
                                                 .selection_start = 1,
                                                 .selection_end = 1}));
        const auto partial = editor->text_input_state();
        REQUIRE(partial.has_value());
        REQUIRE(editor->handle_text_input_event({.type = nk::TextInputEvent::Type::Preedit,
                                                 .text = "\u00E9x",
                                                 .selection_start = 0,
                                                 .selection_end = 2}));
        CHECK(editor->text_input_state()->caret_rect.x == partial->caret_rect.x);
        REQUIRE(editor->handle_text_input_event(
            {.type = nk::TextInputEvent::Type::ClearPreedit, .text = {}}));
        editor->set_text("a\n\u0301");
        REQUIRE(
            editor->handle_text_input_event({.type = nk::TextInputEvent::Type::DeleteSurrounding,
                                             .text = {},
                                             .delete_before_length = 1}));
        CHECK(editor->text() == "a\n");
        REQUIRE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(editor->text() == "a\n\u0301");
        REQUIRE(editor->handle_text_input_event(
            {.type = nk::TextInputEvent::Type::DeleteSurrounding,
             .text = {},
             .delete_before_length = std::numeric_limits<std::size_t>::max(),
             .delete_after_length = std::numeric_limits<std::size_t>::max()}));
        CHECK(editor->text().empty());
        CHECK_FALSE(
            editor->handle_text_input_event({.type = nk::TextInputEvent::Type::DeleteSurrounding,
                                             .text = {},
                                             .delete_before_length = 1}));
    };
    check(nk::TextArea::create());
    check(nk::TextField::create());
}

TEST_CASE("TextArea maps clicks after multiline preedit back to committed offsets",
          "[text_area][ime]") {
    auto area = nk::TextArea::create();
    area->allocate({0, 0, 240, 100});
    area->set_text("old\nsuffix");
    REQUIRE(key(*area, nk::KeyCode::Home, nk::Modifiers::Ctrl));
    REQUIRE(key(*area, nk::KeyCode::End, nk::Modifiers::Shift));
    REQUIRE(area->handle_text_input_event(
        {.type = nk::TextInputEvent::Type::Preedit, .text = "one\ntwo", .selection_end = 7}));
    CHECK(painted_text(*area) == std::vector<std::string>{"one", "two", "suffix"});
    REQUIRE(area->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 8, .y = 50, .button = 1}));
    CHECK(area->cursor_position() == 4);
    REQUIRE(commit(*area, "|"));
    CHECK(area->text() == "old\n|suffix");
}

TEST_CASE("Composition reserves Return for the input context", "[text][ime]") {
    auto search = nk::SearchField::create();
    auto area = nk::TextArea::create();
    int submitted = 0;
    auto connection = search->on_search().connect([&](std::string_view) { ++submitted; });
    for (auto* widget :
         {static_cast<nk::Widget*>(search.get()), static_cast<nk::Widget*>(area.get())}) {
        REQUIRE(widget->handle_text_input_event(
            {.type = nk::TextInputEvent::Type::Preedit, .text = "pending"}));
        REQUIRE(key(*widget, nk::KeyCode::Return));
    }
    CHECK(submitted == 0);
    CHECK(area->text().empty());
    CHECK_FALSE(key(*area, nk::KeyCode::Unknown));
    CHECK(painted_text(*area).empty()); // Unallocated widgets do not paint content.
    area->allocate({0, 0, 240, 100});
    CHECK(painted_text(*area) == std::vector<std::string>{"pending"});
    REQUIRE(commit(*search, "query"));
    REQUIRE(key(*search, nk::KeyCode::Return));
    CHECK(submitted == 1);
    CHECK(connection.connected());
}

TEST_CASE("Secure single-line composition does not paint preedit characters",
          "[text][ime][render]") {
    auto field = nk::TextField::create("secret");
    field->allocate({0, 0, 240, 36});
    field->set_secure_text_entry(true);
    field->select_all();
    REQUIRE(field->handle_text_input_event(
        {.type = nk::TextInputEvent::Type::Preedit, .text = "\u5019\u88DC", .selection_end = 6}));
    CHECK(painted_text(*field) == std::vector<std::string>{"\u2022\u2022"});
}

TEST_CASE("macOS gives marked-text navigation to the input context", "[text][ime][macos]") {
    for (const auto code : {nk::KeyCode::Return,
                            nk::KeyCode::Escape,
                            nk::KeyCode::Left,
                            nk::KeyCode::Right,
                            nk::KeyCode::Up,
                            nk::KeyCode::Down,
                            nk::KeyCode::Backspace,
                            nk::KeyCode::Delete}) {
        CHECK(nk::detail::macos_dispatch_key_directly(code, nk::Modifiers::None, false));
        CHECK_FALSE(nk::detail::macos_dispatch_key_directly(code, nk::Modifiers::None, true));
    }
    CHECK_FALSE(
        nk::detail::macos_dispatch_key_directly(nk::KeyCode::A, nk::Modifiers::None, false));
    CHECK(nk::detail::macos_dispatch_key_directly(nk::KeyCode::C, nk::Modifiers::Super, true));
    CHECK(nk::detail::macos_dispatch_key_directly(nk::KeyCode::Z, nk::Modifiers::Ctrl, true));
}

TEST_CASE("Native UTF-16 ranges map to editor byte offsets", "[text][ime][macos]") {
    const std::string text = "a\U0001F600\u00E9\u4E2D";
    CHECK(nk::detail::utf16_offset_from_utf8(text, text.size()) == 5);
    CHECK(nk::detail::utf8_offset_from_utf16(text, 1) == 1);
    CHECK(nk::detail::utf8_offset_from_utf16(text, 2) == 1);
    CHECK(nk::detail::utf8_offset_from_utf16(text, 2, true) == 5);
    CHECK(nk::detail::utf8_offset_from_utf16(text, 3) == 5);
    CHECK(nk::detail::utf8_offset_from_utf16(text, 4) == 7);
    CHECK(nk::detail::utf8_offset_from_utf16(text, std::numeric_limits<std::size_t>::max()) ==
          text.size());
    CHECK(nk::detail::utf16_offset_from_utf8(text, 6) == 3);
    CHECK(nk::detail::utf16_offset_from_utf8(text, std::numeric_limits<std::size_t>::max()) == 5);
    for (const auto byte : {0U, 1U, 5U, 7U, 10U}) {
        CHECK(nk::detail::utf8_offset_from_utf16(
                  text, nk::detail::utf16_offset_from_utf8(text, byte)) == byte);
    }
}

TEST_CASE("Editors apply input-method replacement ranges as one undoable edit", "[text][ime]") {
    const auto check = [](auto editor) {
        editor->allocate({0, 0, 240, 100});
        editor->set_text("cafe");
        int changes = 0;
        auto connection = editor->on_text_changed().connect([&](auto&&...) { ++changes; });
        REQUIRE(editor->handle_text_input_event({.type = nk::TextInputEvent::Type::Commit,
                                                 .text = "\u00E9",
                                                 .replacement_range = nk::TextInputRange{3, 4}}));
        CHECK(editor->text() == "caf\u00E9");
        CHECK(editor->cursor_position() == 5);
        CHECK(changes == 1);
        REQUIRE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(editor->text() == "cafe");
        CHECK(editor->cursor_position() == 4);
        CHECK_FALSE(editor->has_selection());
        CHECK(connection.connected());
    };
    check(nk::TextArea::create());
    check(nk::TextField::create());
}

TEST_CASE("Preedit replacement ranges compose over committed text", "[text][ime]") {
    const auto check = [](auto editor) {
        editor->allocate({0, 0, 240, 100});
        editor->set_text("ka");
        REQUIRE(editor->handle_text_input_event({.type = nk::TextInputEvent::Type::Preedit,
                                                 .text = "\u304B",
                                                 .selection_start = 3,
                                                 .selection_end = 3,
                                                 .replacement_range = nk::TextInputRange{0, 2}}));
        CHECK(editor->text() == "ka");
        CHECK(painted_text(*editor) == std::vector<std::string>{"\u304B"});
        REQUIRE(commit(*editor, "\u304B"));
        CHECK(editor->text() == "\u304B");
        REQUIRE(key(*editor, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(editor->text() == "ka");
        CHECK(editor->cursor_position() == 2);
        CHECK_FALSE(editor->has_selection());
        // Cancelling restores the caret, so later typing does not replace the target.
        for (const auto type :
             {nk::TextInputEvent::Type::ClearPreedit, nk::TextInputEvent::Type::Preedit}) {
            REQUIRE(
                editor->handle_text_input_event({.type = nk::TextInputEvent::Type::Preedit,
                                                 .text = "x",
                                                 .replacement_range = nk::TextInputRange{0, 1}}));
            REQUIRE(editor->handle_text_input_event({.type = type, .text = {}}));
            CHECK(editor->text() == "ka");
            CHECK(editor->cursor_position() == 2);
            CHECK_FALSE(editor->has_selection());
            CHECK(painted_text(*editor) == std::vector<std::string>{"ka"});
        }
        REQUIRE(commit(*editor, "!"));
        CHECK(editor->text() == "ka!");
        REQUIRE(editor->handle_text_input_event({.type = nk::TextInputEvent::Type::Preedit,
                                                 .text = "x",
                                                 .replacement_range = nk::TextInputRange{0, 1}}));
        editor->set_text("fresh");
        CHECK(editor->cursor_position() == 5);
        CHECK_FALSE(editor->has_selection());
    };
    check(nk::TextArea::create());
    check(nk::TextField::create());
}

TEST_CASE("Input-method replacement ranges keep code points and read-only guards", "[text][ime]") {
    const auto check = [](auto editor) {
        editor->allocate({0, 0, 240, 100});
        editor->set_text("a\u00E9b");
        REQUIRE(editor->handle_text_input_event({.type = nk::TextInputEvent::Type::Commit,
                                                 .text = "x",
                                                 .replacement_range = nk::TextInputRange{2, 99}}));
        CHECK(editor->text() == "ax");
        REQUIRE(editor->handle_text_input_event({.type = nk::TextInputEvent::Type::Commit,
                                                 .text = {},
                                                 .replacement_range = nk::TextInputRange{0, 1}}));
        CHECK(editor->text() == "x");
        editor->set_editable(false);
        CHECK_FALSE(
            editor->handle_text_input_event({.type = nk::TextInputEvent::Type::Commit,
                                             .text = "y",
                                             .replacement_range = nk::TextInputRange{0, 1}}));
        CHECK(editor->text() == "x");
    };
    check(nk::TextArea::create());
    check(nk::TextField::create());
}

TEST_CASE("Window reports composition and withholds secure text from input methods",
          "[text][ime]") {
    nk::Window window({.title = "Input state", .width = 240, .height = 100});
    auto field = nk::TextField::create("secret");
    window.set_child(field);
    field->allocate({10, 10, 220, 36});
    field->grab_focus();
    auto state = window.current_text_input_state();
    REQUIRE(state.has_value());
    CHECK_FALSE(state->composing);
    window.dispatch_text_input_event({.type = nk::TextInputEvent::Type::Preedit, .text = "x"});
    state = window.current_text_input_state();
    REQUIRE(state.has_value());
    CHECK(state->composing);
    CHECK(state->text == "secret");
    REQUIRE(key(*field, nk::KeyCode::Escape));
    state = window.current_text_input_state();
    REQUIRE(state.has_value());
    CHECK_FALSE(state->composing);

    field->set_secure_text_entry(true);
    state = window.current_text_input_state();
    REQUIRE(state.has_value());
    CHECK(state->text.empty());
    CHECK(state->cursor == 0);
    CHECK(state->anchor == 0);
    CHECK(state->caret_rect.height > 0);

    auto area = nk::TextArea::create();
    area->allocate({0, 0, 240, 100});
    REQUIRE(area->handle_text_input_event(
        {.type = nk::TextInputEvent::Type::Preedit, .text = "pending"}));
    CHECK(area->text_input_state()->composing);
    REQUIRE(commit(*area, "done"));
    CHECK_FALSE(area->text_input_state()->composing);
}

TEST_CASE("Native input documents address committed text in UTF-16 units", "[text][ime][native]") {
    using nk::detail::Utf16Range;
    const nk::detail::NativeInputDocument document("a\U0001F600\u00E9", 7, 7);
    CHECK(document.length() == 4);
    CHECK_FALSE(document.marked_range().has_value());
    CHECK(document.selected_range() == Utf16Range{4, 0});

    auto substring = document.substring({1, 2});
    REQUIRE(substring.has_value());
    CHECK(substring->text == "\U0001F600");
    CHECK(substring->range == Utf16Range{1, 2});
    substring = document.substring({2, 1});
    REQUIRE(substring.has_value());
    CHECK(substring->range == Utf16Range{1, 2});
    substring = document.substring({3, std::numeric_limits<std::size_t>::max()});
    REQUIRE(substring.has_value());
    CHECK(substring->text == "\u00E9");
    CHECK_FALSE(document.substring({5, 0}).has_value());
    CHECK(document.clamp({2, 1}) == Utf16Range{1, 2});
    CHECK(document.clamp({9, 0}) == Utf16Range{4, 0});

    CHECK(document.committed_replacement({3, 1}) == nk::TextInputRange{5, 7});
    CHECK_FALSE(document.committed_replacement({4, 0}).has_value());
    CHECK_FALSE(document.committed_replacement({9, 1}).has_value());
}

TEST_CASE("Native input documents place composition over the selection", "[text][ime][native]") {
    using nk::detail::Utf16Range;
    // "b" is selected; the input method composes two kana with the second active.
    const nk::detail::NativeInputDocument document("abc", 2, 1, "\u304B\u306A", 3, 6);
    CHECK(document.length() == 4);
    CHECK(document.marked_range() == Utf16Range{1, 2});
    CHECK(document.selected_range() == Utf16Range{2, 1});
    const auto substring = document.substring({0, 4});
    REQUIRE(substring.has_value());
    CHECK(substring->text == "a\u304B\u306Ac");

    CHECK_FALSE(document.committed_replacement({1, 2}).has_value());
    CHECK_FALSE(document.committed_replacement({2, 1}).has_value());
    CHECK(document.committed_replacement({0, 1}) == nk::TextInputRange{0, 1});
    CHECK(document.committed_replacement({0, 3}) == nk::TextInputRange{0, 2});
    CHECK(document.committed_replacement({3, 1}) == nk::TextInputRange{2, 3});
}
