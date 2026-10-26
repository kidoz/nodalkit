#include "../src/accessibility/accessibility_tree.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <limits>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <stdexcept>

namespace {
class FixedWidget : public nk::Widget {
public:
    FixedWidget(float width, float height) : width_(width), height_(height) {}

    nk::SizeRequest measure(const nk::Constraints&) const override {
        return {width_, height_, width_, height_};
    }

private:
    float width_;
    float height_;
};
} // namespace

TEST_CASE("Box exposes ordered child management without subclassing", "[box]") {
    auto box = nk::Box::create();
    auto first = nk::Box::create();
    auto second = nk::Box::create();
    auto third = nk::Box::create();
    box->append(second);
    box->prepend(first);
    box->insert(99, third);
    REQUIRE(box->children().size() == 3);
    CHECK(box->children()[0] == first);
    CHECK(box->children()[1] == second);
    CHECK(box->children()[2] == third);
    CHECK(second->parent() == box.get());
    box->remove(*second);
    CHECK(second->parent() == nullptr);
    box->insert(1, second);
    CHECK(box->children()[1] == second);
    box->clear();
    CHECK(box->children().empty());
    CHECK(first->parent() == nullptr);
    CHECK(second->parent() == nullptr);
    box->append(first);
    box.reset();
    CHECK(first->parent() == nullptr);
}

TEST_CASE("Box rejects duplicate ownership and cycles without changing the tree",
          "[box][lifetime]") {
    auto box = nk::Box::create();
    auto child = nk::Box::create();
    box->append(child);
    CHECK_THROWS_AS(box->append(nullptr), std::invalid_argument);
    CHECK_THROWS_AS(box->append(box), std::invalid_argument);
    CHECK_THROWS_AS(box->append(child), std::invalid_argument);
    CHECK_THROWS_AS(child->append(box), std::invalid_argument);
    auto other = nk::Box::create();
    CHECK_THROWS_AS(other->insert(0, child), std::invalid_argument);
    CHECK(box->children().size() == 1);
    CHECK(other->children().empty());
    box->remove(*child);
    other->append(child);
    CHECK(child->parent() == other.get());
    nk::Window window;
    auto root = nk::Box::create();
    window.set_child(root);
    CHECK_THROWS_AS(box->append(root), std::invalid_argument);
    window.set_child(nullptr);
    box->append(root);
    CHECK(root->parent() == box.get());
}

TEST_CASE("Box measures and allocates rows and columns with padding", "[box][layout]") {
    auto box = nk::Box::create(nk::Orientation::Horizontal, 5);
    auto first = std::make_shared<FixedWidget>(10, 20);
    auto second = std::make_shared<FixedWidget>(30, 40);
    box->append(first);
    box->append(second);
    box->set_padding({.top = 2, .right = 3, .bottom = 4, .left = 6});
    const auto horizontal = box->measure(nk::Constraints::unbounded());
    CHECK(horizontal.natural_width == 54);
    CHECK(horizontal.natural_height == 46);
    box->allocate({10, 20, 54, 46});
    CHECK(first->allocation() == nk::Rect{16, 22, 10, 20});
    CHECK(second->allocation() == nk::Rect{31, 22, 30, 40});
    REQUIRE_FALSE(box->debug_pending_layout());
    box->set_orientation(nk::Orientation::Vertical);
    CHECK(box->debug_pending_layout());
    CHECK(box->orientation() == nk::Orientation::Vertical);
    const auto vertical = box->measure(nk::Constraints::unbounded());
    CHECK(vertical.natural_width == 39);
    CHECK(vertical.natural_height == 71);
    box->allocate({10, 20, 39, 71});
    CHECK(second->allocation() == nk::Rect{16, 47, 30, 40});
    box->set_homogeneous(true);
    REQUIRE(box->homogeneous());
    REQUIRE(box->debug_pending_layout());
    box->allocate({0, 0, 39, 91});
    CHECK(first->allocation().height == second->allocation().height);
    CHECK(first->allocation().height == Catch::Approx(40));
}

TEST_CASE("Box passes inner width to wrapping children", "[box][layout]") {
    class WrappedWidget : public nk::Widget {
    public:
        bool has_height_for_width() const override { return true; }

        float height_for_width(float width) const override { return width / 2; }
    };

    auto box = nk::Box::create();
    box->set_padding({.top = 3, .right = 10, .bottom = 4, .left = 10});
    box->append(std::make_shared<WrappedWidget>());
    REQUIRE(box->has_height_for_width());
    CHECK(box->height_for_width(120) == 57);
}

TEST_CASE("Box spacing changes relayout and reject invalid geometry", "[box][layout]") {
    auto box = nk::Box::create();
    box->allocate({0, 0, 100, 100});
    box->set_spacing(12);
    CHECK(box->spacing() == 12);
    CHECK(box->debug_pending_layout());
    CHECK_THROWS_AS(box->set_spacing(-1), std::invalid_argument);
    CHECK_THROWS_AS(box->set_spacing(std::numeric_limits<float>::infinity()),
                    std::invalid_argument);
    CHECK_THROWS_AS(box->set_spacing(std::numeric_limits<float>::quiet_NaN()),
                    std::invalid_argument);
    CHECK(box->spacing() == 12);
}

TEST_CASE("Box preserves descendant accessibility and keyboard order", "[box][accessibility]") {
    nk::Window window;
    auto box = nk::Box::create();
    auto first = nk::Button::create("First");
    auto second = nk::Button::create("Second");
    box->append(first);
    box->append(second);
    window.set_child(box);
    nk::detail::AccessibilityTree tree(window);
    const auto ids = tree.root_children();
    REQUIRE(ids.size() == 2);
    CHECK(tree.info(ids[0])->name == "First");
    CHECK(tree.info(ids[1])->name == "Second");
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    CHECK(tree.focused() == ids[0]);
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Tab});
    CHECK(tree.focused() == ids[1]);
    box->clear();
    CHECK_FALSE(tree.focused().has_value());
    CHECK(tree.root_children().empty());
    CHECK_FALSE(tree.info(ids[0]).has_value());
}
