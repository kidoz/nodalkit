#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/render/renderer.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/search_field.h>
#include <nk/widgets/text_field.h>
#include <string>
#include <utility>
#include <vector>

namespace {

bool press(nk::Widget& widget, nk::KeyCode key, nk::Modifiers modifiers = nk::Modifiers::None) {
    return widget.handle_key_event(
        {.type = nk::KeyEvent::Type::Press, .key = key, .modifiers = modifiers});
}

bool commit(nk::Widget& widget, std::string text) {
    return widget.handle_text_input_event(
        {.type = nk::TextInputEvent::Type::Commit, .text = std::move(text)});
}

} // namespace

TEST_CASE("SearchField shares clipboard selection and undo with text fields", "[search][editing]") {
    auto source = nk::TextField::create("caf\u00E9");
    source->select_all();
    REQUIRE(press(*source, nk::KeyCode::C, nk::Modifiers::Ctrl));
    auto field = nk::SearchField::create();
    field->allocate({0, 0, 220, 36});
    REQUIRE(press(*field, nk::KeyCode::V, nk::Modifiers::Ctrl));
    CHECK(field->text() == "caf\u00E9");
    REQUIRE(press(*field, nk::KeyCode::A, nk::Modifiers::Ctrl));
    REQUIRE(commit(*field, "needle"));
    CHECK(field->text() == "needle");
    REQUIRE(press(*field, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(field->text() == "caf\u00E9");
    REQUIRE(press(*field, nk::KeyCode::Z, nk::Modifiers::Ctrl | nk::Modifiers::Shift));
    CHECK(field->text() == "needle");
    REQUIRE(press(*field, nk::KeyCode::A, nk::Modifiers::Super));
    REQUIRE(press(*field, nk::KeyCode::X, nk::Modifiers::Super));
    CHECK(field->text().empty());
    REQUIRE(press(*field, nk::KeyCode::V, nk::Modifiers::Super));
    CHECK(field->text() == "needle");
}

TEST_CASE("SearchField composes text and cancels preedit before clearing the query",
          "[search][ime]") {
    auto field = nk::SearchField::create("Find");
    field->allocate({10, 12, 220, 36});
    field->set_text("ab");
    int changes = 0;
    auto connection = field->on_text_changed().connect([&](std::string_view) { ++changes; });
    REQUIRE(press(*field, nk::KeyCode::Home));
    REQUIRE(press(*field, nk::KeyCode::Right));
    REQUIRE(field->handle_text_input_event(
        {.type = nk::TextInputEvent::Type::Preedit, .text = "\u5019", .selection_end = 3}));
    CHECK(field->text() == "ab");
    CHECK(changes == 0);
    REQUIRE(field->text_input_state().has_value());
    CHECK(field->text_input_state()->cursor == 1);
    REQUIRE(press(*field, nk::KeyCode::Escape));
    CHECK(field->text() == "ab");
    CHECK(changes == 0);
    REQUIRE(commit(*field, "\u88DC"));
    CHECK(field->text() == "a\u88DCb");
    CHECK(changes == 1);
    REQUIRE(field->handle_text_input_event({.type = nk::TextInputEvent::Type::DeleteSurrounding,
                                            .text = {},
                                            .delete_before_length = 3,
                                            .delete_after_length = 1}));
    CHECK(field->text() == "a");
    REQUIRE(press(*field, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(field->text() == "a\u88DCb");
    CHECK(field->accessible()->value() == "a\u88DCb");
}

TEST_CASE("SearchField clear is cancellable and undoable and Enter searches once",
          "[search][clear]") {
    auto field = nk::SearchField::create();
    field->allocate({0, 0, 200, 36});
    field->set_text("query");
    int changes = 0;
    int searches = 0;
    auto changed = field->on_text_changed().connect([&](std::string_view) { ++changes; });
    auto searched = field->on_search().connect([&](std::string_view query) {
        CHECK(query == "query");
        ++searches;
    });
    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 185, .y = 18, .button = 1}));
    CHECK(field->text() == "query");
    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 240, .y = 18, .button = 1}));
    CHECK(changes == 0);
    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 185, .y = 18, .button = 1}));
    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 185, .y = 18, .button = 1}));
    CHECK(field->text().empty());
    CHECK(changes == 1);
    REQUIRE(press(*field, nk::KeyCode::Z, nk::Modifiers::Ctrl));
    CHECK(field->text() == "query");
    REQUIRE(press(*field, nk::KeyCode::Return));
    CHECK(searches == 1);
    REQUIRE(press(*field, nk::KeyCode::Escape));
    CHECK(field->text().empty());
    CHECK_FALSE(press(*field, nk::KeyCode::Escape));
}

TEST_CASE("SearchField exposes a scrolled caret and selection through Window dispatch",
          "[search][input]") {
    nk::Window window({.title = "Search input", .width = 240, .height = 80});
    auto field = nk::SearchField::create("Find");
    window.set_child(field);
    field->allocate({10, 10, 180, 36});
    field->grab_focus();
    window.dispatch_text_input_event(
        {.type = nk::TextInputEvent::Type::Commit, .text = std::string(100, 'w')});
    REQUIRE(field->text().size() == 100);
    const auto end = window.current_text_input_state();
    REQUIRE(end.has_value());
    CHECK(end->cursor == 100);
    CHECK(end->caret_rect.x >= 38);
    CHECK(end->caret_rect.right() <= 162);
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press,
                               .key = nk::KeyCode::A,
                               .modifiers = nk::Modifiers::Ctrl});
    const auto selected = window.current_text_input_state();
    REQUIRE(selected.has_value());
    CHECK(selected->anchor == 0);
    CHECK(selected->cursor == 100);
    field->set_sensitive(false);
    window.dispatch_text_input_event({.type = nk::TextInputEvent::Type::Commit, .text = "blocked"});
    CHECK(field->text().size() == 100);
    CHECK_FALSE(window.current_text_input_state().has_value());
}

TEST_CASE("SearchField pointer selection does not activate its clear control",
          "[search][pointer]") {
    auto field = nk::SearchField::create();
    field->allocate({0, 0, 220, 36});
    field->set_text("alpha beta");
    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 32, .y = 18, .button = 1, .click_count = 2}));
    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 32, .y = 18, .button = 1}));
    REQUIRE(commit(*field, "word"));
    CHECK(field->text() == "word beta");

    field->set_text("alpha beta");
    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 28, .y = 18, .button = 1}));
    REQUIRE(field->handle_mouse_event({.type = nk::MouseEvent::Type::Move, .x = 208, .y = 18}));
    REQUIRE(field->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Release, .x = 208, .y = 18, .button = 1}));
    CHECK(field->text() == "alpha beta");
    REQUIRE(commit(*field, "replacement"));
    CHECK(field->text() == "replacement");
}

TEST_CASE("Shared single-line editing keeps surrounding deletion UTF-8 safe and undoable",
          "[search][text][ime]") {
    auto search = nk::SearchField::create();
    auto plain = nk::TextField::create();
    for (auto* field : {static_cast<nk::TextField*>(search.get()), plain.get()}) {
        field->allocate({0, 0, 220, 36});
        field->set_text(std::string(100, 'w'));
        REQUIRE(field->text_input_caret_rect().has_value());
        CHECK(field->text_input_caret_rect()->right() <= 220.0F);
        field->set_text("a\u00E9b");
        REQUIRE(press(*field, nk::KeyCode::Left));
        REQUIRE(field->handle_text_input_event({.type = nk::TextInputEvent::Type::DeleteSurrounding,
                                                .text = {},
                                                .delete_before_length = 1}));
        CHECK(field->text() == "ab");
        REQUIRE(press(*field, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(field->text() == "a\u00E9b");
        REQUIRE(press(*field, nk::KeyCode::Home));
        REQUIRE(press(*field, nk::KeyCode::Right));
        REQUIRE(field->handle_text_input_event({.type = nk::TextInputEvent::Type::DeleteSurrounding,
                                                .text = {},
                                                .delete_after_length = 1}));
        CHECK(field->text() == "ab");
        REQUIRE(press(*field, nk::KeyCode::Z, nk::Modifiers::Ctrl));
        CHECK(field->text() == "a\u00E9b");
        const auto committed_caret = field->text_input_caret_rect();
        REQUIRE(field->handle_text_input_event(
            {.type = nk::TextInputEvent::Type::Preedit, .text = "pending", .selection_end = 7}));
        field->set_editable(false);
        CHECK_FALSE(press(*field, nk::KeyCode::Y, nk::Modifiers::Ctrl));
        CHECK_FALSE(commit(*field, "blocked"));
        CHECK_FALSE(field->handle_text_input_event(
            {.type = nk::TextInputEvent::Type::Preedit, .text = "blocked"}));
        CHECK(field->text() == "a\u00E9b");
        CHECK_FALSE(field->text_input_state().has_value());
        field->set_editable(true);
        REQUIRE(field->text_input_caret_rect().has_value());
        REQUIRE(committed_caret.has_value());
        CHECK(field->text_input_caret_rect()->x == committed_caret->x);
    }
}

TEST_CASE("Search selection stays out of the icon and clear-control pixels", "[search][render]") {
    auto field = nk::SearchField::create();
    field->allocate({0, 0, 180, 36});
    field->set_text(std::string(100, 'w'));
    nk::SoftwareRenderer renderer;
    auto pixels = [&] {
        nk::SnapshotContext ctx;
        static_cast<nk::Widget&>(*field).snapshot(ctx);
        const auto root = ctx.take_root();
        renderer.begin_frame({180, 36}, 1.0F);
        renderer.render(*root);
        renderer.end_frame();
        return std::vector<uint8_t>(renderer.pixel_data(), renderer.pixel_data() + 180 * 36 * 4);
    };
    const auto before = pixels();
    field->select_all();
    const auto selected = pixels();
    bool changed_inside = false;
    for (int y = 0; y < 36; ++y) {
        for (int x = 0; x < 180; ++x) {
            const auto i = static_cast<std::size_t>((y * 180 + x) * 4);
            const bool same =
                std::equal(before.begin() + i, before.begin() + i + 4, selected.begin() + i);
            if (x < 28 || x >= 152) {
                REQUIRE(same);
            } else {
                changed_inside = changed_inside || !same;
            }
        }
    }
    CHECK(changed_inside);
}
