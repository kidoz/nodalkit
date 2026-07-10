/// @file widget_controls_test.cpp
/// @brief Headless unit tests for interactive form-control widgets: state
/// round-trips, change-signal emission, idempotence, range clamping, and
/// radio-group exclusivity. No Window/event loop required.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nk/accessibility/atspi_bridge.h>
#include <nk/foundation/types.h>
#include <nk/render/render_node.h>
#include <nk/render/snapshot_context.h>
#include <nk/widgets/button.h>
#include <nk/widgets/calendar.h>
#include <nk/widgets/check_box.h>
#include <nk/widgets/color_well.h>
#include <nk/widgets/headerbar.h>
#include <nk/widgets/radio_button.h>
#include <nk/widgets/search_field.h>
#include <nk/widgets/slider.h>
#include <nk/widgets/spin_button.h>
#include <nk/widgets/switch_widget.h>
#include <nk/widgets/text_area.h>
#include <string>

TEST_CASE("CheckBox toggles state and emits on change only", "[widgets][check_box]") {
    auto check = nk::CheckBox::create("Enable");
    REQUIRE(check->label() == "Enable");
    REQUIRE_FALSE(check->is_checked());

    int toggles = 0;
    bool last = false;
    auto conn = check->on_toggled().connect([&](bool checked) {
        ++toggles;
        last = checked;
    });

    check->set_checked(true);
    REQUIRE(check->is_checked());
    REQUIRE(toggles == 1);
    REQUIRE(last);

    // Setting the same value is a no-op: no re-emit.
    check->set_checked(true);
    REQUIRE(toggles == 1);

    check->set_checked(false);
    REQUIRE_FALSE(check->is_checked());
    REQUIRE(toggles == 2);
    REQUIRE_FALSE(last);

    check->set_label("Renamed");
    REQUIRE(check->label() == "Renamed");
    conn.disconnect();
}

TEST_CASE("Switch toggles active state and emits on change only", "[widgets][switch]") {
    auto sw = nk::Switch::create();
    REQUIRE_FALSE(sw->is_active());

    int toggles = 0;
    auto conn = sw->on_toggled().connect([&](bool) { ++toggles; });

    sw->set_active(true);
    REQUIRE(sw->is_active());
    REQUIRE(toggles == 1);
    sw->set_active(true);
    REQUIRE(toggles == 1);
    sw->set_active(false);
    REQUIRE(toggles == 2);
    conn.disconnect();
}

TEST_CASE("RadioGroup enforces single selection", "[widgets][radio_button]") {
    auto group = nk::RadioGroup::create();
    auto a = nk::RadioButton::create("A");
    auto b = nk::RadioButton::create("B");
    a->set_group(group);
    b->set_group(group);

    int a_selected = 0;
    int b_selected = 0;
    auto ca = a->on_selected().connect([&] { ++a_selected; });
    auto cb = b->on_selected().connect([&] { ++b_selected; });

    group->select(a.get());
    REQUIRE(a->is_selected());
    REQUIRE_FALSE(b->is_selected());
    REQUIRE(a_selected == 1);

    group->select(b.get());
    REQUIRE(b->is_selected());
    REQUIRE_FALSE(a->is_selected());
    REQUIRE(b_selected == 1);

    // Re-selecting the already-selected button does not re-emit.
    group->select(b.get());
    REQUIRE(b_selected == 1);
    ca.disconnect();
    cb.disconnect();
}

TEST_CASE("Slider clamps to range, snaps to step, and emits on change", "[widgets][slider]") {
    auto slider = nk::Slider::create();
    REQUIRE(slider->orientation() == nk::Orientation::Horizontal);
    REQUIRE(slider->minimum() == Catch::Approx(0.0));
    REQUIRE(slider->maximum() == Catch::Approx(1.0));
    REQUIRE(slider->value() == Catch::Approx(0.0));

    slider->set_range(0.0, 10.0);
    int changes = 0;
    double last = -1.0;
    auto conn = slider->on_value_changed().connect([&](double v) {
        ++changes;
        last = v;
    });

    slider->set_value(4.0);
    REQUIRE(slider->value() == Catch::Approx(4.0));
    REQUIRE(changes == 1);
    REQUIRE(last == Catch::Approx(4.0));

    // No re-emit on identical value.
    slider->set_value(4.0);
    REQUIRE(changes == 1);

    // Clamp above the maximum and below the minimum.
    slider->set_value(99.0);
    REQUIRE(slider->value() == Catch::Approx(10.0));
    slider->set_value(-5.0);
    REQUIRE(slider->value() == Catch::Approx(0.0));

    // Step snapping: a value lands on the nearest multiple of the step.
    slider->set_step(2.0);
    slider->set_value(4.0);
    REQUIRE(slider->value() == Catch::Approx(4.0));
    conn.disconnect();
}

TEST_CASE("SpinButton clamps to range and reports precision", "[widgets][spin_button]") {
    auto spin = nk::SpinButton::create();
    spin->set_range(0.0, 10.0);
    spin->set_precision(2);
    REQUIRE(spin->precision() == 2);

    int changes = 0;
    auto conn = spin->on_value_changed().connect([&](double) { ++changes; });

    spin->set_value(5.0);
    REQUIRE(spin->value() == Catch::Approx(5.0));
    REQUIRE(changes == 1);

    spin->set_value(20.0);
    REQUIRE(spin->value() == Catch::Approx(10.0));
    spin->set_value(-1.0);
    REQUIRE(spin->value() == Catch::Approx(0.0));
    conn.disconnect();
}

TEST_CASE("SearchField round-trips text and emits on programmatic change",
          "[widgets][search_field]") {
    auto field = nk::SearchField::create("Search files");
    REQUIRE(field->placeholder() == "Search files");
    REQUIRE(field->text().empty());

    int changes = 0;
    std::string last;
    auto conn = field->on_text_changed().connect([&](std::string_view text) {
        ++changes;
        last = std::string(text);
    });

    field->set_text("query");
    REQUIRE(field->text() == "query");
    REQUIRE(changes == 1);
    REQUIRE(last == "query");
    field->set_text("query");
    REQUIRE(changes == 1);

    field->set_placeholder("Find");
    REQUIRE(field->placeholder() == "Find");
    conn.disconnect();
}

TEST_CASE("TextArea round-trips text and editable/visible-rows state", "[widgets][text_area]") {
    auto area = nk::TextArea::create();
    REQUIRE(area->text().empty());
    REQUIRE(area->is_editable());

    int changes = 0;
    auto conn = area->on_text_changed().connect([&] { ++changes; });

    area->set_text("line one\nline two");
    REQUIRE(area->text() == "line one\nline two");
    REQUIRE(changes == 1);
    area->set_text("line one\nline two");
    REQUIRE(changes == 1);

    area->set_editable(false);
    REQUIRE_FALSE(area->is_editable());
    area->set_visible_rows(5);
    REQUIRE(area->visible_rows() == 5);
    area->set_placeholder("Notes");
    REQUIRE(area->placeholder() == "Notes");
    conn.disconnect();
}

TEST_CASE("ColorWell round-trips its color", "[widgets][color_well]") {
    auto well = nk::ColorWell::create();
    const nk::Color red{1.0F, 0.0F, 0.0F, 1.0F};
    well->set_color(red);
    REQUIRE(well->color() == red);
}

TEST_CASE("Calendar selects dates and navigates months with wraparound", "[widgets][calendar]") {
    auto cal = nk::Calendar::create();

    const nk::Date date{2026, 6, 15};
    cal->set_selected_date(date);
    REQUIRE(cal->selected_date() == date);

    cal->set_displayed_month(2026, 12);
    REQUIRE(cal->displayed_year() == 2026);
    REQUIRE(cal->displayed_month() == 12);

    cal->go_next_month();
    REQUIRE(cal->displayed_year() == 2027);
    REQUIRE(cal->displayed_month() == 1);

    cal->set_displayed_month(2026, 1);
    cal->go_previous_month();
    REQUIRE(cal->displayed_year() == 2025);
    REQUIRE(cal->displayed_month() == 12);
}

namespace {

class SnapshotButton : public nk::Button {
public:
    static std::shared_ptr<SnapshotButton> create(std::string label) {
        return std::shared_ptr<SnapshotButton>(new SnapshotButton(std::move(label)));
    }

    void snapshot_for_test(nk::SnapshotContext& ctx) const { snapshot(ctx); }

private:
    explicit SnapshotButton(std::string label) : nk::Button(std::move(label)) {}
};

class SnapshotHeaderbar : public nk::Headerbar {
public:
    static std::shared_ptr<SnapshotHeaderbar> create(std::string title) {
        return std::shared_ptr<SnapshotHeaderbar>(new SnapshotHeaderbar(std::move(title)));
    }

    void snapshot_for_test(nk::SnapshotContext& ctx) const { snapshot(ctx); }

private:
    explicit SnapshotHeaderbar(std::string title) : nk::Headerbar(std::move(title)) {}
};

class FixedWidget final : public nk::Widget {
public:
    static std::shared_ptr<FixedWidget> create(float width) {
        return std::shared_ptr<FixedWidget>(new FixedWidget(width));
    }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {width_, 30.0F, width_, 30.0F};
    }

private:
    explicit FixedWidget(float width) : width_(width) {}

    float width_ = 0.0F;
};

const nk::TextNode* find_text_node(const nk::RenderNode& node) {
    if (node.kind() == nk::RenderNodeKind::Text) {
        return static_cast<const nk::TextNode*>(&node);
    }
    for (const auto& child : node.children()) {
        if (child) {
            if (const auto* found = find_text_node(*child)) {
                return found;
            }
        }
    }
    return nullptr;
}

const nk::TextNode* find_text_node(const nk::RenderNode& node, std::string_view text) {
    if (node.kind() == nk::RenderNodeKind::Text) {
        const auto* text_node = static_cast<const nk::TextNode*>(&node);
        if (text_node->text() == text) {
            return text_node;
        }
    }
    for (const auto& child : node.children()) {
        if (child) {
            if (const auto* found = find_text_node(*child, text)) {
                return found;
            }
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Button labels elide instead of painting past the body", "[widgets][button]") {
    const std::string label = "Show Preferences Sheet";

    SECTION("an under-allocated button truncates with an ellipsis") {
        auto button = SnapshotButton::create(label);
        button->allocate({0.0F, 0.0F, 80.0F, 36.0F});
        nk::SnapshotContext ctx;
        button->snapshot_for_test(ctx);

        const auto root = ctx.take_root();
        REQUIRE(root != nullptr);
        const auto* text = find_text_node(*root);
        REQUIRE(text != nullptr);
        REQUIRE(text->text() != label);
        REQUIRE(text->text().ends_with("…"));
    }

    SECTION("a naturally sized button keeps its full label") {
        auto button = SnapshotButton::create(label);
        button->allocate({0.0F, 0.0F, 400.0F, 36.0F});
        nk::SnapshotContext ctx;
        button->snapshot_for_test(ctx);

        const auto root = ctx.take_root();
        REQUIRE(root != nullptr);
        const auto* text = find_text_node(*root);
        REQUIRE(text != nullptr);
        REQUIRE(text->text() == label);
    }
}

TEST_CASE("Headerbar keeps actions clear of its title and exposes window controls",
          "[widgets][headerbar][accessibility]") {
    auto headerbar = SnapshotHeaderbar::create("Document title");
    auto leading = FixedWidget::create(64.0F);
    auto trailing = FixedWidget::create(72.0F);
    headerbar->add_leading(leading);
    headerbar->add_trailing(trailing);
    headerbar->allocate({0.0F, 0.0F, 520.0F, 46.0F});

    nk::SnapshotContext context;
    headerbar->snapshot_for_test(context);
    const auto root = context.take_root();
    REQUIRE(root != nullptr);
    const auto* title = find_text_node(*root, "Document title");
    REQUIRE(title != nullptr);
    CHECK(title->bounds().x >= leading->allocation().right());
    CHECK(title->bounds().right() <= trailing->allocation().x);

    std::vector<std::string> accessible_controls;
    for (const auto& child : headerbar->children()) {
        if (const auto* accessible = child->accessible();
            accessible != nullptr && accessible->role() == nk::AccessibleRole::Button &&
            child->is_visible()) {
            accessible_controls.emplace_back(accessible->name());
            CHECK(accessible->supports_action(nk::AccessibleAction::Activate));
        }
    }
    CHECK(accessible_controls == std::vector<std::string>{"Minimize", "Maximize", "Close"});
    REQUIRE(headerbar->accessible() != nullptr);
    CHECK(headerbar->accessible()->role() == nk::AccessibleRole::Group);
    CHECK(nk::atspi_role_name("group") == "panel");
    CHECK(headerbar->centering_policy() == nk::HeaderbarCenteringPolicy::Strict);
}

TEST_CASE("Headerbar centers titles, follows decoration layout, and exposes back navigation",
          "[widgets][headerbar][accessibility]") {
    auto headerbar = SnapshotHeaderbar::create("Centered");
    headerbar->set_decoration_layout("close:");
    headerbar->set_show_back_button(true);
    headerbar->allocate({0.0F, 0.0F, 800.0F, 46.0F});

    nk::SnapshotContext context;
    headerbar->snapshot_for_test(context);
    const auto root = context.take_root();
    REQUIRE(root != nullptr);
    const auto* title = find_text_node(*root, "Centered");
    REQUIRE(title != nullptr);
    constexpr float estimated_title_width = 8.0F * 14.0F * 0.55F;
    CHECK(title->bounds().x == Catch::Approx((800.0F - estimated_title_width) * 0.5F));
    CHECK(headerbar->decoration_layout() == "close:");
    CHECK(headerbar->shows_back_button());

    bool back_requested = false;
    auto connection = headerbar->on_back_requested().connect([&] { back_requested = true; });
    bool saw_back = false;
    bool saw_visible_close_at_start = false;
    for (const auto& child : headerbar->children()) {
        const auto* accessible = child->accessible();
        if (accessible == nullptr || accessible->role() != nk::AccessibleRole::Button) {
            continue;
        }
        CHECK(accessible->description() == accessible->name());
        if (accessible->name() == "Back") {
            saw_back = child->is_visible();
            REQUIRE(accessible->perform_action(nk::AccessibleAction::Activate));
        } else if (accessible->name() == "Close") {
            saw_visible_close_at_start = child->is_visible() && child->allocation().x < 100.0F;
        } else {
            CHECK_FALSE(child->is_visible());
        }
    }
    CHECK(saw_back);
    CHECK(saw_visible_close_at_start);
    CHECK(back_requested);
    connection.disconnect();
}
