/// @file widget_views_test.cpp
/// @brief Unit tests for navigation, container, and display widgets:
/// item/selection management, expand/split state, severity, and round-trip
/// properties. The motion tests attach a widget to a window; the rest do not.

#include <array>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <nk/foundation/types.h>
#include <nk/layout/breakpoint.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/platform/window.h>
#include <nk/platform/window_inspector.h>
#include <nk/render/render_node.h>
#include <nk/render/snapshot_context.h>
#include <nk/runtime/event_loop.h>
#include <nk/style/theme_selection.h>
#include <nk/ui_core/state_flags.h>
#include <nk/widgets/avatar.h>
#include <nk/widgets/badge.h>
#include <nk/widgets/banner.h>
#include <nk/widgets/box.h>
#include <nk/widgets/breadcrumb.h>
#include <nk/widgets/button.h>
#include <nk/widgets/clamp.h>
#include <nk/widgets/context_menu.h>
#include <nk/widgets/expander.h>
#include <nk/widgets/headerbar.h>
#include <nk/widgets/info_bar.h>
#include <nk/widgets/label.h>
#include <nk/widgets/menu_button.h>
#include <nk/widgets/navigation_split_view.h>
#include <nk/widgets/navigation_stack.h>
#include <nk/widgets/overlay_split_view.h>
#include <nk/widgets/popover.h>
#include <nk/widgets/preferences.h>
#include <nk/widgets/progress_bar.h>
#include <nk/widgets/scroll_area.h>
#include <nk/widgets/search_bar.h>
#include <nk/widgets/search_field.h>
#include <nk/widgets/segmented_control.h>
#include <nk/widgets/separator.h>
#include <nk/widgets/spinner.h>
#include <nk/widgets/split_view.h>
#include <nk/widgets/status_page.h>
#include <nk/widgets/tab_bar.h>
#include <nk/widgets/toast_overlay.h>
#include <nk/widgets/toolbar_view.h>
#include <nk/widgets/tooltip.h>
#include <nk/widgets/view_stack.h>
#include <nk/widgets/visual_effect_view.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

class FixedSizeWidget final : public nk::Widget {
public:
    [[nodiscard]] static std::shared_ptr<FixedSizeWidget> create(float width, float height) {
        return std::shared_ptr<FixedSizeWidget>(new FixedSizeWidget(width, height));
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, height_, width_, height_};
    }

private:
    FixedSizeWidget(float width, float height) : width_(width), height_(height) {}

    float width_ = 0.0F;
    float height_ = 0.0F;
};

nk::Widget* deepest_hit(nk::Widget& widget, nk::Point point) {
    if (!widget.is_visible() || !widget.hit_test(point)) {
        return nullptr;
    }
    const auto children = widget.children();
    for (auto iterator = children.rbegin(); iterator != children.rend(); ++iterator) {
        if (*iterator != nullptr) {
            if (auto* hit = deepest_hit(**iterator, point); hit != nullptr) {
                return hit;
            }
        }
    }
    return &widget;
}

bool dispatch_mouse_bubble(nk::Widget* target, const nk::MouseEvent& event) {
    for (auto* current = target; current != nullptr; current = current->parent()) {
        if (current->handle_mouse_event(event)) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("TabBar manages tabs and selection", "[widgets][tab_bar]") {
    auto bar = nk::TabBar::create();
    REQUIRE(bar->tab_count() == 0);
    REQUIRE(bar->selected_index() < 0);

    bar->append_tab("One");
    // The first appended tab becomes selected.
    REQUIRE(bar->tab_count() == 1);
    REQUIRE(bar->selected_index() == 0);

    bar->append_tab("Two");
    bar->append_tab("Three");
    REQUIRE(bar->tab_count() == 3);
    REQUIRE(bar->tab_label(2) == "Three");

    int changes = 0;
    int last = -1;
    auto conn = bar->on_selection_changed().connect([&](int index) {
        ++changes;
        last = index;
    });

    bar->set_selected_index(2);
    REQUIRE(bar->selected_index() == 2);
    REQUIRE(changes == 1);
    REQUIRE(last == 2);
    // Selecting the current tab does not re-emit.
    bar->set_selected_index(2);
    REQUIRE(changes == 1);

    bar->set_tab_label(0, "First");
    REQUIRE(bar->tab_label(0) == "First");

    bar->remove_tab(0);
    REQUIRE(bar->tab_count() == 2);

    bar->clear_tabs();
    REQUIRE(bar->tab_count() == 0);
    conn.disconnect();
}

TEST_CASE("TabBar throws on out-of-range selection", "[widgets][tab_bar]") {
    auto bar = nk::TabBar::create();
    bar->append_tab("Only");
    REQUIRE_THROWS_AS(bar->set_selected_index(5), std::out_of_range);
}

TEST_CASE("Expander toggles and emits on change only", "[widgets][expander]") {
    auto expander = nk::Expander::create("Details");
    REQUIRE(expander->title() == "Details");
    REQUIRE_FALSE(expander->is_expanded());

    int changes = 0;
    bool last = false;
    auto conn = expander->on_expanded_changed().connect([&](bool expanded) {
        ++changes;
        last = expanded;
    });

    expander->set_expanded(true);
    REQUIRE(expander->is_expanded());
    REQUIRE(changes == 1);
    REQUIRE(last);
    expander->set_expanded(true);
    REQUIRE(changes == 1);

    expander->set_child(nk::Label::create("Body"));
    expander->set_title("More");
    REQUIRE(expander->title() == "More");
    conn.disconnect();
}

TEST_CASE("SplitView clamps the divider position and emits on change", "[widgets][split_view]") {
    auto split = nk::SplitView::create(nk::Orientation::Vertical);
    REQUIRE(split->orientation() == nk::Orientation::Vertical);

    split->set_start_child(nk::Label::create("start"));
    split->set_end_child(nk::Label::create("end"));

    int changes = 0;
    auto conn = split->on_position_changed().connect([&](float) { ++changes; });

    split->set_position(0.25F);
    REQUIRE(split->position() == Catch::Approx(0.25F));
    REQUIRE(changes == 1);

    // Clamped into [0, 1].
    split->set_position(2.0F);
    REQUIRE(split->position() == Catch::Approx(1.0F));
    split->set_position(-1.0F);
    REQUIRE(split->position() == Catch::Approx(0.0F));
    conn.disconnect();
}

TEST_CASE("BreakpointBin publishes allocation-dependent state", "[layout][breakpoint]") {
    auto bin = nk::BreakpointBin::create();
    auto breakpoint = nk::Breakpoint::create({
        .min_width = std::nullopt,
        .max_width = 400.0F,
        .min_height = std::nullopt,
        .max_height = std::nullopt,
    });
    bin->set_child(nk::Label::create("Adaptive"));
    bin->add_breakpoint(breakpoint);

    std::vector<bool> changes;
    auto connection =
        breakpoint->on_active_changed().connect([&](bool active) { changes.push_back(active); });
    bin->allocate({0.0F, 0.0F, 600.0F, 400.0F});
    CHECK_FALSE(breakpoint->is_active());
    bin->allocate({0.0F, 0.0F, 360.0F, 400.0F});
    CHECK(breakpoint->is_active());
    bin->allocate({0.0F, 0.0F, 700.0F, 400.0F});
    CHECK_FALSE(breakpoint->is_active());
    CHECK(changes == std::vector<bool>{true, false});
    bin->remove_breakpoint(*breakpoint);
    CHECK(bin->breakpoints().empty());
    connection.disconnect();
}

TEST_CASE("Clamp centers content within its maximum size", "[widgets][clamp]") {
    auto clamp = nk::Clamp::create();
    auto content = nk::Label::create("Readable content");
    clamp->set_child(content);
    clamp->set_maximum_size(600.0F);
    clamp->set_tightening_threshold(400.0F);
    clamp->set_scales_with_text(false);
    clamp->allocate({0.0F, 0.0F, 1000.0F, 300.0F});

    CHECK(clamp->child() == content.get());
    CHECK(clamp->maximum_size() == Catch::Approx(600.0F));
    CHECK(content->allocation().width == Catch::Approx(600.0F));
    CHECK(content->allocation().x == Catch::Approx(200.0F));
}

TEST_CASE("NavigationSplitView switches between sidebar and content",
          "[widgets][navigation_split_view]") {
    auto split = nk::NavigationSplitView::create();
    auto sidebar = nk::Label::create("Sidebar");
    auto content = nk::Label::create("Content");
    split->set_sidebar(sidebar);
    split->set_content(content);
    split->set_min_sidebar_width(200.0F);
    split->set_max_sidebar_width(300.0F);
    split->set_sidebar_width_fraction(0.4F);
    split->allocate({0.0F, 0.0F, 800.0F, 500.0F});
    CHECK(sidebar->is_visible());
    CHECK(content->is_visible());
    CHECK(sidebar->allocation().width == Catch::Approx(300.0F));

    split->set_collapsed(true);
    split->set_show_content(false);
    split->allocate({0.0F, 0.0F, 360.0F, 500.0F});
    CHECK(sidebar->is_visible());
    CHECK_FALSE(content->is_visible());
    split->set_show_content(true);
    split->allocate({0.0F, 0.0F, 360.0F, 500.0F});
    CHECK_FALSE(sidebar->is_visible());
    CHECK(content->is_visible());
    CHECK(content->allocation().width == Catch::Approx(360.0F));
}

TEST_CASE("NavigationSplitView hides the sidebar in the expanded layout",
          "[widgets][navigation_split_view]") {
    auto split = nk::NavigationSplitView::create();
    auto sidebar = nk::Label::create("Sidebar");
    auto content = nk::Label::create("Content");
    split->set_sidebar(sidebar);
    split->set_content(content);
    split->set_min_sidebar_width(200.0F);
    split->set_max_sidebar_width(300.0F);
    split->set_sidebar_width_fraction(0.4F);
    CHECK(split->shows_sidebar());

    int sidebar_changes = 0;
    auto connection = split->on_show_sidebar_changed().connect(
        [&](bool shown) { sidebar_changes += shown ? 1 : -1; });

    split->set_show_sidebar(false);
    split->allocate({0.0F, 0.0F, 800.0F, 500.0F});
    CHECK_FALSE(split->shows_sidebar());
    CHECK(sidebar_changes == -1);
    CHECK_FALSE(sidebar->is_visible());
    CHECK(content->is_visible());
    CHECK(content->allocation().x == Catch::Approx(0.0F));
    CHECK(content->allocation().width == Catch::Approx(800.0F));

    split->set_show_sidebar(true);
    split->allocate({0.0F, 0.0F, 800.0F, 500.0F});
    CHECK(sidebar->is_visible());
    CHECK(sidebar->allocation().width == Catch::Approx(300.0F));
    CHECK(sidebar_changes == 0);

    // Compact navigation still presents the sidebar, independent of the
    // expanded-layout preference.
    split->set_show_sidebar(false);
    split->set_collapsed(true);
    split->set_show_content(false);
    split->allocate({0.0F, 0.0F, 360.0F, 500.0F});
    CHECK(sidebar->is_visible());
    CHECK_FALSE(content->is_visible());
    connection.disconnect();
}

TEST_CASE("OverlaySplitView overlays and dismisses its sidebar", "[widgets][overlay_split_view]") {
    auto split = nk::OverlaySplitView::create();
    auto sidebar = nk::Label::create("Tools");
    auto content = nk::Label::create("Document");
    split->set_sidebar(sidebar);
    split->set_content(content);
    split->set_sidebar_width(260.0F);
    split->set_collapsed(true);
    split->allocate({0.0F, 0.0F, 420.0F, 500.0F});
    CHECK(sidebar->is_visible());
    CHECK(sidebar->allocation().width == Catch::Approx(260.0F));
    CHECK(content->allocation().width == Catch::Approx(420.0F));

    split->set_show_sidebar(false);
    split->allocate({0.0F, 0.0F, 420.0F, 500.0F});
    CHECK_FALSE(sidebar->is_visible());
    CHECK_FALSE(split->shows_sidebar());
}

TEST_CASE("ToolbarView coordinates page bars and edge extension", "[widgets][toolbar_view]") {
    auto view = nk::ToolbarView::create();
    auto top = nk::Label::create("Top");
    auto bottom = nk::Label::create("Bottom");
    auto content = nk::Label::create("Content");
    view->set_content(content);
    view->add_top_bar(top);
    view->add_bottom_bar(bottom);
    view->set_top_bar_style(nk::ToolbarStyle::Raised);
    view->set_bottom_bar_style(nk::ToolbarStyle::RaisedBorder);
    view->allocate({0.0F, 0.0F, 600.0F, 400.0F});

    CHECK(view->content() == content.get());
    CHECK(view->top_bars().size() == 1);
    CHECK(view->bottom_bars().size() == 1);
    CHECK(view->top_bar_height() > 0.0F);
    CHECK(view->bottom_bar_height() > 0.0F);
    CHECK(content->allocation().y == Catch::Approx(view->top_bar_height()));

    view->set_extend_content_to_top_edge(true);
    view->set_extend_content_to_bottom_edge(true);
    view->allocate({0.0F, 0.0F, 600.0F, 400.0F});
    CHECK(content->allocation().y == Catch::Approx(0.0F));
    CHECK(content->allocation().height == Catch::Approx(400.0F));
}

TEST_CASE("ToolbarView paint layers do not block content input", "[widgets][toolbar_view][input]") {
    SECTION("button clicks reach content") {
        auto view = nk::ToolbarView::create();
        auto top = nk::Label::create("Top");
        auto button = nk::Button::create("Activate");
        bool activated = false;
        auto connection = button->on_clicked().connect([&] { activated = true; });
        view->set_content(button);
        view->add_top_bar(top);
        view->allocate({0.0F, 0.0F, 320.0F, 240.0F});

        const nk::Point point{120.0F, 120.0F};
        auto* target = deepest_hit(*view, point);
        REQUIRE(target == button.get());
        REQUIRE(dispatch_mouse_bubble(
            target,
            {.type = nk::MouseEvent::Type::Press, .x = point.x, .y = point.y, .button = 1}));
        REQUIRE(dispatch_mouse_bubble(
            target,
            {.type = nk::MouseEvent::Type::Release, .x = point.x, .y = point.y, .button = 1}));
        CHECK(activated);
        connection.disconnect();
    }

    SECTION("scroll events reach content") {
        auto view = nk::ToolbarView::create();
        auto scroll = nk::ScrollArea::create();
        scroll->set_content(FixedSizeWidget::create(300.0F, 800.0F));
        view->set_content(scroll);
        view->add_top_bar(nk::Label::create("Top"));
        view->allocate({0.0F, 0.0F, 320.0F, 240.0F});

        const nk::Point point{120.0F, 120.0F};
        auto* target = deepest_hit(*view, point);
        REQUIRE(target != nullptr);
        REQUIRE(dispatch_mouse_bubble(target,
                                      {.type = nk::MouseEvent::Type::Scroll,
                                       .x = point.x,
                                       .y = point.y,
                                       .scroll_dy = -1.0F}));
        CHECK(scroll->v_offset() > 0.0F);
    }
}

TEST_CASE("Preferences compose accessible rows, groups, and clamped pages",
          "[widgets][preferences][accessibility]") {
    auto row = nk::PreferencesRow::create("Dark Style", "Follow the system appearance");
    auto suffix = nk::Label::create("System");
    row->set_suffix(suffix);
    row->set_activatable(true);
    bool activated = false;
    auto connection = row->on_activated().connect([&] { activated = true; });
    REQUIRE(row->accessible() != nullptr);
    CHECK(row->accessible()->role() == nk::AccessibleRole::Button);
    REQUIRE(row->accessible()->perform_action(nk::AccessibleAction::Activate));
    CHECK(activated);

    auto group = nk::PreferencesGroup::create("Appearance");
    group->add(row);
    auto page = nk::PreferencesPage::create("Preferences", "Configure the application");
    page->add(group);
    page->allocate({0.0F, 0.0F, 1000.0F, 700.0F});
    CHECK(page->groups().size() == 1);
    CHECK(group->rows().size() == 1);
    CHECK(group->allocation().width == Catch::Approx(720.0F));
    CHECK(row->suffix() == suffix.get());
    connection.disconnect();
}

TEST_CASE("Banner, status page, and toast overlay expose GNOME feedback patterns",
          "[widgets][feedback]") {
    auto banner = nk::Banner::create("Network connection lost");
    banner->set_button_label("Retry");
    bool retried = false;
    auto banner_connection = banner->on_button_clicked().connect([&] { retried = true; });
    REQUIRE(banner->accessible() != nullptr);
    REQUIRE(banner->accessible()->perform_action(nk::AccessibleAction::Activate));
    CHECK(retried);
    banner->set_revealed(false);
    CHECK_FALSE(banner->is_visible());

    auto status = nk::StatusPage::create("No Documents", "Open a document to get started");
    auto icon = FixedSizeWidget::create(48.0F, 48.0F);
    auto action = nk::Label::create("Open…");
    status->set_icon(icon);
    status->set_action(action);
    status->allocate({0.0F, 0.0F, 800.0F, 600.0F});
    CHECK(status->icon() == icon.get());
    CHECK(status->action() == action.get());
    CHECK(icon->allocation().bottom() < action->allocation().y);
    CHECK(action->allocation().width > 0.0F);

    auto overlay = nk::ToastOverlay::create();
    auto content = nk::Label::create("Document");
    overlay->set_child(content);
    overlay->add_toast(
        {.title = "Saved", .action_label = {}, .priority = nk::ToastPriority::Normal});
    overlay->add_toast(
        {.title = "Connection Lost", .action_label = "Retry", .priority = nk::ToastPriority::High});
    REQUIRE(overlay->current_toast() != nullptr);
    CHECK(overlay->current_toast()->title == "Connection Lost");
    CHECK(overlay->toast_count() == 2);
    bool action_requested = false;
    auto action_connection = overlay->on_action().connect([&] {
        action_requested = true;
        overlay->dismiss_current();
    });
    REQUIRE(overlay->accessible() != nullptr);
    REQUIRE(
        overlay->children().back()->accessible()->perform_action(nk::AccessibleAction::Activate));
    CHECK(action_requested);
    REQUIRE(overlay->current_toast() != nullptr);
    CHECK(overlay->current_toast()->title == "Saved");
    CHECK(overlay->toast_count() == 1);
    overlay->dismiss_all();
    CHECK(overlay->current_toast() == nullptr);
    CHECK(overlay->child() == content.get());
    banner_connection.disconnect();
}

TEST_CASE("ToastOverlay automatically dismisses transient feedback", "[widgets][feedback]") {
    nk::EventLoop event_loop;
    auto overlay = nk::ToastOverlay::create();
    overlay->add_toast({
        .title = "Saved",
        .action_label = {},
        .priority = nk::ToastPriority::Normal,
        .timeout = std::chrono::milliseconds::zero(),
    });

    CHECK(overlay->toast_count() == 1);
    REQUIRE(event_loop.poll());
    CHECK(overlay->toast_count() == 0);
}

TEST_CASE("InfoBar round-trips message, severity, and closable", "[widgets][info_bar]") {
    auto bar = nk::InfoBar::create("Saved", nk::InfoBarSeverity::Success);
    REQUIRE(bar->message() == "Saved");
    REQUIRE(bar->severity() == nk::InfoBarSeverity::Success);
    REQUIRE(bar->is_closable());

    bar->set_message("Failed");
    bar->set_severity(nk::InfoBarSeverity::Error);
    bar->set_closable(false);
    REQUIRE(bar->message() == "Failed");
    REQUIRE(bar->severity() == nk::InfoBarSeverity::Error);
    REQUIRE_FALSE(bar->is_closable());
}

TEST_CASE("Breadcrumb builds, extends, and truncates its path", "[widgets][breadcrumb]") {
    auto crumb = nk::Breadcrumb::create();
    crumb->set_path({"Home", "Projects", "NodalKit"});
    REQUIRE(crumb->depth() == 3);
    REQUIRE(crumb->segment(0) == "Home");
    REQUIRE(crumb->segment(2) == "NodalKit");

    crumb->push("src");
    REQUIRE(crumb->depth() == 4);
    REQUIRE(crumb->segment(3) == "src");

    crumb->pop_to(2);
    REQUIRE(crumb->depth() == 2);
    REQUIRE(crumb->segment(1) == "Projects");
}

TEST_CASE("ContextMenu returns focus to the widget that was active", "[widgets][context_menu]") {
    nk::Window window({.title = "Context menu focus", .width = 320, .height = 240});
    auto root = nk::Box::create(nk::Orientation::Vertical, 0.0F);
    auto button = nk::Button::create("Edit");
    auto menu = nk::ContextMenu::create();
    menu->add_item("Cut");
    root->append(button);
    root->append(menu);
    window.set_child(root);

    button->grab_focus();
    REQUIRE(nk::has_flag(button->state_flags(), nk::StateFlags::Focused));

    menu->show_at({12.0F, 12.0F});
    CHECK(menu->is_open());
    CHECK(nk::has_flag(menu->state_flags(), nk::StateFlags::Focused));
    CHECK_FALSE(nk::has_flag(button->state_flags(), nk::StateFlags::Focused));

    menu->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Escape});
    CHECK_FALSE(menu->is_open());
    CHECK(nk::has_flag(button->state_flags(), nk::StateFlags::Focused));
    CHECK_FALSE(nk::has_flag(menu->state_flags(), nk::StateFlags::Focused));
}

TEST_CASE("MenuButton closes when focus moves away", "[widgets][menu_button]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Menu button", .width = 320, .height = 240});
    auto root = nk::Box::create(nk::Orientation::Vertical, 8.0F);
    auto menu_button = nk::MenuButton::create("File");
    menu_button->set_items({nk::NativeMenuItem::action("Quit", "app.quit")});
    auto other = nk::Button::create("Other");
    root->append(menu_button);
    root->append(other);
    window.set_child(root);
    menu_button->allocate({8.0F, 8.0F, 140.0F, 36.0F});

    menu_button->grab_focus();
    REQUIRE(menu_button->handle_key_event(
        {.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return}));
    CHECK(menu_button->is_open());
    CHECK(nk::has_flag(menu_button->state_flags(), nk::StateFlags::Focused) == false);

    other->grab_focus();
    REQUIRE(app.event_loop().poll());
    CHECK_FALSE(menu_button->is_open());
    CHECK(nk::has_flag(other->state_flags(), nk::StateFlags::Focused));
}

TEST_CASE("ContextMenu manages items and starts closed", "[widgets][context_menu]") {
    auto menu = nk::ContextMenu::create();
    REQUIRE(menu->item_count() == 0);
    REQUIRE_FALSE(menu->is_open());

    menu->add_item("Cut");
    menu->add_item("Copy");
    menu->add_separator();
    menu->add_item("Paste");
    REQUIRE(menu->item_count() == 4);

    menu->dismiss();
    REQUIRE_FALSE(menu->is_open());

    menu->clear();
    REQUIRE(menu->item_count() == 0);
}

TEST_CASE("Popover starts closed and accepts content", "[widgets][popover]") {
    auto popover = nk::Popover::create();
    REQUIRE_FALSE(popover->is_open());
    popover->set_child(nk::Label::create("content"));
    popover->dismiss();
    REQUIRE_FALSE(popover->is_open());
}

TEST_CASE("Popover flips inside the window and activates its content", "[widgets][popover]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Popover", .width = 320, .height = 240});
    auto anchor = nk::Button::create("Anchor");
    auto action = nk::Button::create("Do it");
    auto popover = nk::Popover::create();
    popover->set_child(action);
    auto root = nk::Box::create(nk::Orientation::Vertical, 0.0F);
    root->append(anchor);
    root->append(popover);
    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());

    anchor->grab_focus();
    popover->set_anchor(*anchor);
    popover->show();
    CHECK(popover->is_open());
    CHECK(action->allocation().y >= anchor->allocation().bottom());
    CHECK(nk::has_flag(action->state_flags(), nk::StateFlags::Focused));

    int clicks = 0;
    [[maybe_unused]] auto connection = action->on_clicked().connect([&clicks] { ++clicks; });
    const auto action_center = nk::Point{
        action->allocation().x + (action->allocation().width * 0.5F),
        action->allocation().y + (action->allocation().height * 0.5F),
    };
    CHECK(popover->handle_mouse_event({.type = nk::MouseEvent::Type::Press,
                                       .x = action_center.x,
                                       .y = action_center.y,
                                       .button = 1}));
    CHECK(popover->handle_mouse_event({.type = nk::MouseEvent::Type::Release,
                                       .x = action_center.x,
                                       .y = action_center.y,
                                       .button = 1}));
    CHECK(clicks == 1);

    CHECK(
        popover->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Escape}));
    CHECK_FALSE(popover->is_open());
    CHECK(nk::has_flag(anchor->state_flags(), nk::StateFlags::Focused));

    popover->show_at({160.0F, 220.0F});
    CHECK(action->allocation().bottom() < 220.0F);
    popover->handle_mouse_event(
        {.type = nk::MouseEvent::Type::Press, .x = 8.0F, .y = 8.0F, .button = 1});
    CHECK_FALSE(popover->is_open());
}

namespace {

struct PaintedShape {
    nk::Rect bounds{};
    nk::Color color{};

    bool operator==(const PaintedShape&) const = default;
};

void collect_rounded_rects(const nk::RenderNode& node, std::vector<PaintedShape>& out) {
    if (node.kind() == nk::RenderNodeKind::RoundedRect) {
        const auto& rounded = static_cast<const nk::RoundedRectNode&>(node);
        out.push_back({node.bounds(), rounded.color()});
    }
    for (const auto& child : node.children()) {
        if (child) {
            collect_rounded_rects(*child, out);
        }
    }
}

std::vector<PaintedShape> paint_rounded_rects(nk::Widget& widget, nk::Rect allocation) {
    widget.allocate(allocation);
    nk::SnapshotContext snapshot;
    widget.snapshot(snapshot);
    auto root = snapshot.take_root();
    std::vector<PaintedShape> shapes;
    if (root) {
        collect_rounded_rects(*root, shapes);
    }
    return shapes;
}

void pump(nk::EventLoop& loop, std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
        loop.poll();
    }
}

int timer_count(const nk::EventLoop& loop, std::string_view label) {
    int count = 0;
    for (const auto& event : loop.debug_trace_events()) {
        if (event.source_label == label) {
            ++count;
        }
    }
    return count;
}

bool pump_until(nk::EventLoop& loop,
                std::chrono::milliseconds budget,
                const std::function<bool()>& done) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{5});
        loop.poll();
    }
    return done();
}

} // namespace

TEST_CASE("ProgressBar round-trips fraction including indeterminate", "[widgets][progress_bar]") {
    auto bar = nk::ProgressBar::create();
    bar->set_fraction(0.5F);
    REQUIRE(bar->fraction() == Catch::Approx(0.5F));

    // A negative fraction marks the bar indeterminate.
    bar->set_fraction(-1.0F);
    REQUIRE(bar->fraction() < 0.0F);

    // pulse() advances the indeterminate animation without crashing.
    bar->pulse();
}

TEST_CASE("Spinner round-trips spinning state and diameter", "[widgets][spinner]") {
    auto spinner = nk::Spinner::create();
    spinner->set_spinning(true);
    REQUIRE(spinner->is_spinning());
    spinner->set_spinning(false);
    REQUIRE_FALSE(spinner->is_spinning());

    spinner->set_diameter(48.0F);
    REQUIRE(spinner->diameter() == Catch::Approx(48.0F));
}

TEST_CASE("Spinner snapshot does not advance the indicator", "[widgets][spinner][motion]") {
    auto spinner = nk::Spinner::create();
    const nk::Rect allocation{0.0F, 0.0F, 32.0F, 32.0F};
    const auto first = paint_rounded_rects(*spinner, allocation);
    const auto second = paint_rounded_rects(*spinner, allocation);
    REQUIRE_FALSE(first.empty());
    REQUIRE(first == second);
}

TEST_CASE("Spinner clock stops for hide, detach, and a hidden window",
          "[widgets][spinner][motion]") {
    nk::Application app(0, nullptr);
    app.set_system_preferences_observation_enabled(false);
    nk::ThemeSelection motion;
    motion.motion_policy = nk::MotionPolicy::Normal;
    app.set_theme_selection(motion);
    nk::Window window({.title = "Spinner motion", .width = 320, .height = 240});
    auto spinner = nk::Spinner::create();
    window.set_child(spinner);
    window.present();

    const nk::Rect allocation{0.0F, 0.0F, 32.0F, 32.0F};
    const auto initial = paint_rounded_rects(*spinner, allocation);
    REQUIRE(pump_until(app.event_loop(), std::chrono::milliseconds{300}, [&] {
        return paint_rounded_rects(*spinner, allocation) != initial;
    }));
    const auto moving = paint_rounded_rects(*spinner, allocation);
    REQUIRE(paint_rounded_rects(*spinner, allocation) == moving);

    spinner->set_visible(false);
    app.event_loop().clear_debug_trace_events();
    pump(app.event_loop(), std::chrono::milliseconds{80});
    REQUIRE(timer_count(app.event_loop(), "spinner-motion") == 0);

    spinner->set_visible(true);
    const auto held = paint_rounded_rects(*spinner, allocation);
    REQUIRE(paint_rounded_rects(*spinner, allocation) == held);
    REQUIRE(pump_until(app.event_loop(), std::chrono::milliseconds{300}, [&] {
        return paint_rounded_rects(*spinner, allocation) != held;
    }));

    nk::ThemeSelection reduced;
    reduced.motion_policy = nk::MotionPolicy::Reduced;
    app.set_theme_selection(reduced);
    app.event_loop().clear_debug_trace_events();
    pump(app.event_loop(), std::chrono::milliseconds{80});
    REQUIRE(timer_count(app.event_loop(), "spinner-motion") == 0);
    const auto parked = paint_rounded_rects(*spinner, allocation);
    REQUIRE(paint_rounded_rects(*spinner, allocation) == parked);

    app.set_theme_selection(motion);
    REQUIRE(pump_until(app.event_loop(), std::chrono::milliseconds{300}, [&] {
        return paint_rounded_rects(*spinner, allocation) != parked;
    }));

    window.hide();
    app.event_loop().clear_debug_trace_events();
    pump(app.event_loop(), std::chrono::milliseconds{80});
    REQUIRE(timer_count(app.event_loop(), "spinner-motion") == 0);

    window.present();
    const auto hidden = paint_rounded_rects(*spinner, allocation);
    REQUIRE(pump_until(app.event_loop(), std::chrono::milliseconds{300}, [&] {
        return paint_rounded_rects(*spinner, allocation) != hidden;
    }));

    window.set_child(nullptr);
    app.event_loop().clear_debug_trace_events();
    pump(app.event_loop(), std::chrono::milliseconds{80});
    REQUIRE(timer_count(app.event_loop(), "spinner-motion") == 0);
}

TEST_CASE("ProgressBar clock follows time, keeps pulse, and honors reduced motion",
          "[widgets][progress_bar][motion]") {
    nk::Application app(0, nullptr);
    app.set_system_preferences_observation_enabled(false);
    nk::ThemeSelection motion;
    motion.motion_policy = nk::MotionPolicy::Normal;
    app.set_theme_selection(motion);
    nk::Window window({.title = "Progress motion", .width = 320, .height = 240});
    auto bar = nk::ProgressBar::create();
    bar->set_fraction(-1.0F);
    window.set_child(bar);
    window.present();

    const nk::Rect allocation{0.0F, 0.0F, 200.0F, 8.0F};
    const auto initial = paint_rounded_rects(*bar, allocation);
    REQUIRE(pump_until(app.event_loop(), std::chrono::milliseconds{300}, [&] {
        return paint_rounded_rects(*bar, allocation) != initial;
    }));
    const auto moving = paint_rounded_rects(*bar, allocation);
    REQUIRE(paint_rounded_rects(*bar, allocation) == moving);

    bar->set_fraction(0.4F);
    app.event_loop().clear_debug_trace_events();
    pump(app.event_loop(), std::chrono::milliseconds{80});
    REQUIRE(timer_count(app.event_loop(), "progress-motion") == 0);
    const auto determinate = paint_rounded_rects(*bar, allocation);
    REQUIRE(paint_rounded_rects(*bar, allocation) == determinate);

    nk::ThemeSelection reduced;
    reduced.motion_policy = nk::MotionPolicy::Reduced;
    app.set_theme_selection(reduced);
    bar->set_fraction(-1.0F);
    const auto parked = paint_rounded_rects(*bar, allocation);
    app.event_loop().clear_debug_trace_events();
    pump(app.event_loop(), std::chrono::milliseconds{80});
    REQUIRE(timer_count(app.event_loop(), "progress-motion") == 0);
    REQUIRE(paint_rounded_rects(*bar, allocation) == parked);

    bar->pulse();
    const auto stepped = paint_rounded_rects(*bar, allocation);
    REQUIRE(stepped != parked);
    bar->pulse();
    REQUIRE(paint_rounded_rects(*bar, allocation) != stepped);
}

TEST_CASE("Badge and Tooltip round-trip their text", "[widgets][badge][tooltip]") {
    auto badge = nk::Badge::create("3");
    REQUIRE(badge->text() == "3");
    badge->set_text("99+");
    REQUIRE(badge->text() == "99+");

    auto tooltip = nk::Tooltip::create("Hint");
    REQUIRE(tooltip->text() == "Hint");
    tooltip->set_text("Updated hint");
    REQUIRE(tooltip->text() == "Updated hint");
}

namespace {

void append_snapshot_detail(const nk::FrameDiagnostics::RenderSnapshotNode& node,
                            std::string& out) {
    out += node.detail;
    out += '\n';
    for (const auto& child : node.children) {
        append_snapshot_detail(child, out);
    }
}

bool frame_contains(const nk::Window& window, std::string_view text) {
    std::string detail;
    append_snapshot_detail(window.inspector().last_frame_diagnostics().render_snapshot, detail);
    return detail.find(text) != std::string::npos;
}

} // namespace

TEST_CASE("Widget tooltip describes the control and follows hover and keyboard focus",
          "[widgets][tooltip]") {
    nk::Application app(0, nullptr);
    nk::Window window({.title = "Tooltip", .width = 320, .height = 240});
    auto button = nk::Button::create("Save");
    button->set_tooltip("Save document");
    CHECK(button->tooltip() == "Save document");
    CHECK(button->ensure_accessible().description() == "Save document");
    auto root = nk::Box::create(nk::Orientation::Vertical, 8.0F);
    root->append(button);
    root->append(nk::Button::create("Other"));
    window.set_child(root);
    window.present();
    REQUIRE(app.event_loop().poll());
    CHECK_FALSE(frame_contains(window, "Save document"));

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    REQUIRE(app.event_loop().poll());
    CHECK(frame_contains(window, "Save document"));

    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    REQUIRE(app.event_loop().poll());
    CHECK_FALSE(frame_contains(window, "Save document"));

    const auto target = button->allocation().origin();
    window.dispatch_mouse_event(
        {.type = nk::MouseEvent::Type::Move, .x = target.x + 8.0F, .y = target.y + 8.0F});
    REQUIRE(app.event_loop().poll());
    CHECK_FALSE(frame_contains(window, "Save document"));

    std::this_thread::sleep_for(std::chrono::milliseconds{450});
    REQUIRE(app.event_loop().poll());
    REQUIRE(app.event_loop().poll());
    CHECK(frame_contains(window, "Save document"));

    window.dispatch_mouse_event({.type = nk::MouseEvent::Type::Move, .x = -20.0F, .y = -20.0F});
    REQUIRE(app.event_loop().poll());
    CHECK_FALSE(frame_contains(window, "Save document"));

    button->set_tooltip("");
    CHECK(button->tooltip().empty());
    CHECK(button->ensure_accessible().description().empty());
}

TEST_CASE("Avatar round-trips initials, diameter, and image", "[widgets][avatar]") {
    auto avatar = nk::Avatar::create("AB");
    REQUIRE(avatar->initials() == "AB");
    avatar->set_initials("CD");
    REQUIRE(avatar->initials() == "CD");

    avatar->set_diameter(64.0F);
    REQUIRE(avatar->diameter() == Catch::Approx(64.0F));

    // A small ARGB8888 image is accepted and can be cleared without crashing.
    const std::array<std::uint32_t, 4> pixels{0xFFFFFFFFU, 0xFF000000U, 0xFFFF0000U, 0xFF00FF00U};
    avatar->set_image(pixels.data(), 2, 2);
    avatar->clear_image();
}

TEST_CASE("Separator reports its orientation", "[widgets][separator]") {
    auto horizontal = nk::Separator::create();
    REQUIRE(horizontal->orientation() == nk::Orientation::Horizontal);
    auto vertical = nk::Separator::create(nk::Orientation::Vertical);
    REQUIRE(vertical->orientation() == nk::Orientation::Vertical);
}

TEST_CASE("VisualEffectView round-trips material, radius, and child",
          "[widgets][visual_effect_view]") {
    auto effect = nk::VisualEffectView::create();
    REQUIRE(effect->material() == nk::VisualEffectMaterial::Sidebar);

    effect->set_material(nk::VisualEffectMaterial::Menu);
    REQUIRE(effect->material() == nk::VisualEffectMaterial::Menu);

    effect->set_corner_radius(8.0F);
    REQUIRE(effect->corner_radius() == Catch::Approx(8.0F));

    auto child = nk::Label::create("inner");
    effect->set_child(child);
    REQUIRE(effect->child() == child.get());

    effect->set_fallback_tint(nk::Color{0.1F, 0.2F, 0.3F, 1.0F});
    effect->clear_fallback_tint();
}
