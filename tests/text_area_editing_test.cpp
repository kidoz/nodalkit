#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/render/render_node.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
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

