// Install-smoke test: verifies that the installed NodalKit SDK exports a
// usable public API surface across every module listed in the README
// architecture table. This test is intentionally headless — it must not
// construct an Application or a Window, so it can run on any host without
// a display.
//
// The goal is not runtime correctness: the individual test suites under
// tests/ cover that. The goal is to prove that every public header is
// reachable through pkg-config and that every public type links against
// the installed libNodalKit.

#include <nk/actions/action.h>
#include <nk/actions/shortcut.h>
#include <nk/foundation/logging.h>
#include <nk/foundation/property.h>
#include <nk/foundation/result.h>
#include <nk/foundation/signal.h>
#include <nk/foundation/types.h>
#include <nk/layout/box_layout.h>
#include <nk/layout/grid_layout.h>
#include <nk/layout/stack_layout.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/style/theme.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/image_view.h>
#include <nk/widgets/label.h>
#include <nk/widgets/list_view.h>
#include <nk/widgets/menu_bar.h>
#include <nk/widgets/scroll_area.h>
#include <nk/widgets/segmented_control.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/text_field.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, char const* what) {
    if (!condition) {
        std::fprintf(stderr, "install_smoke: FAIL: %s\n", what);
        ++failures;
    }
}

void check_foundation() {
    nk::Signal<int> signal;
    int observed = 0;
    auto conn = signal.connect([&observed](int v) { observed = v; });
    signal.emit(42);
    check(conn.connected(), "Signal connection stays live");
    check(observed == 42, "Signal delivers value");

    nk::Property<int> prop{7};
    int last = 0;
    auto prop_conn = prop.on_changed().connect([&last](int const& v) { last = v; });
    prop.set(11);
    check(prop.get() == 11, "Property stores new value");
    check(last == 11, "Property fires on_changed");
    check(prop_conn.connected(), "Property connection stays live");

    nk::Result<int, std::string> ok{5};
    check(ok.has_value() && ok.value() == 5, "Result success path");
    nk::Result<int, std::string> bad{nk::Unexpected<std::string>{"nope"}};
    check(!bad.has_value() && bad.error() == "nope", "Result error path");

    NK_LOG_INFO("install_smoke", "foundation OK");
}

void check_layout() {
    nk::BoxLayout vertical{nk::Orientation::Vertical};
    vertical.set_spacing(4.0F);
    vertical.set_homogeneous(true);
    check(vertical.spacing() == 4.0F, "BoxLayout spacing roundtrip");
    check(vertical.homogeneous(), "BoxLayout homogeneous flag");

    nk::BoxLayout horizontal{nk::Orientation::Horizontal};
    check(horizontal.spacing() == 0.0F, "BoxLayout default spacing");

    nk::GridLayout grid;
    grid.set_row_spacing(2.0F);
    grid.set_column_spacing(3.0F);
    check(grid.row_spacing() == 2.0F, "GridLayout row spacing roundtrip");
    check(grid.column_spacing() == 3.0F, "GridLayout column spacing roundtrip");

    nk::StackLayout stack;
    (void)stack;
}

void check_model() {
    nk::StringListModel model{std::vector<std::string>{"alpha", "beta", "gamma"}};
    check(model.row_count() == 3, "StringListModel row count");
    check(model.display_text(1) == "beta", "StringListModel display text");
    model.append("delta");
    check(model.row_count() == 4, "StringListModel append");

    nk::SelectionModel single{nk::SelectionMode::Single};
    single.select(2);
    check(single.is_selected(2), "SelectionModel single select");
    single.select(5);
    check(single.is_selected(5) && !single.is_selected(2),
          "SelectionModel single mode replaces prior selection");

    nk::SelectionModel multi{nk::SelectionMode::Multiple};
    multi.select(0);
    multi.select(2);
    check(multi.is_selected(0) && multi.is_selected(2),
          "SelectionModel multi select");
    multi.toggle(0);
    check(!multi.is_selected(0) && multi.is_selected(2),
          "SelectionModel toggle deselect");
    multi.clear();
    check(!multi.is_selected(2), "SelectionModel clear");
}

void check_actions() {
    auto action = std::make_shared<nk::Action>("app.quit");
    int activations = 0;
    auto conn = action->on_activated().connect([&activations] { ++activations; });
    action->activate();
    check(activations == 1, "Action fires on_activated");
    check(action->is_enabled(), "Action enabled by default");
    action->set_enabled(false);
    action->activate();
    check(activations == 1, "Disabled Action does not fire");
    check(conn.connected(), "Action connection stays live");

    nk::ActionGroup group;
    group.add(action);
    check(group.lookup("app.quit") == action.get(), "ActionGroup lookup by name");

    nk::Shortcut shortcut{.key_code = 83, .modifiers = nk::Modifiers::Ctrl};
    check(shortcut.key_code == 83, "Shortcut aggregate init");
    nk::ShortcutBinding binding{.shortcut = shortcut, .action_name = "app.quit"};
    check(binding.action_name == "app.quit", "ShortcutBinding aggregate init");
}

void check_style() {
    auto theme = nk::Theme::make_light();
    check(theme != nullptr, "Theme::make_light returns a theme");
    theme->set_token("smoke-token", nk::StyleValue{std::string{"value"}});
    auto const* token = theme->token("smoke-token");
    check(token != nullptr, "Theme token is retrievable");

    auto dark = nk::Theme::make_dark();
    check(dark != nullptr, "Theme::make_dark returns a theme");
}

void check_widgets() {
    auto label = nk::Label::create("hello");
    check(label != nullptr && label->text() == "hello", "Label::create");

    auto button = nk::Button::create("click");
    check(button != nullptr && button->label() == "click", "Button::create");

    auto text_field = nk::TextField::create("seed");
    check(text_field != nullptr && text_field->text() == "seed", "TextField::create");

    auto scroll = nk::ScrollArea::create();
    check(scroll != nullptr, "ScrollArea::create");
    scroll->set_content(label);
    check(scroll->content() == label.get(), "ScrollArea holds content");

    auto list_view = nk::ListView::create();
    check(list_view != nullptr, "ListView::create");
    auto list_model = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{"row-0", "row-1"});
    list_view->set_model(list_model);
    check(list_view->model() == list_model.get(), "ListView::set_model");

    auto dialog = nk::Dialog::create("Confirm", "Save changes?");
    check(dialog != nullptr, "Dialog::create");
    dialog->add_button("Cancel", nk::DialogResponse::Cancel);
    dialog->add_button("Save", nk::DialogResponse::Accept);
    check(!dialog->is_presented(), "Dialog starts unpresented");

    auto combo = nk::ComboBox::create();
    check(combo != nullptr, "ComboBox::create");
    combo->set_items({"one", "two", "three"});
    check(combo->item_count() == 3, "ComboBox::set_items");
    combo->set_selected_index(1);
    check(combo->selected_index() == 1, "ComboBox selection roundtrip");

    auto image_view = nk::ImageView::create();
    check(image_view != nullptr, "ImageView::create");

    auto menu_bar = nk::MenuBar::create();
    check(menu_bar != nullptr, "MenuBar::create");

    auto status_bar = nk::StatusBar::create();
    check(status_bar != nullptr, "StatusBar::create");
    status_bar->set_segments({"Ready", "0 items"});
    check(status_bar->segment_count() == 2, "StatusBar::set_segments");

    auto segmented = nk::SegmentedControl::create();
    check(segmented != nullptr, "SegmentedControl::create");
    segmented->set_segments({"A", "B"});
    check(segmented->segment_count() == 2, "SegmentedControl::set_segments");
    check(segmented->selected_index() == 0, "SegmentedControl auto-selects first");
}

} // namespace

int main() {
    check_foundation();
    check_layout();
    check_model();
    check_actions();
    check_style();
    check_widgets();

    if (failures != 0) {
        std::fprintf(stderr, "install_smoke: %d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
