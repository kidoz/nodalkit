// Drives the macOS accessibility bridge through the same NSAccessibility
// methods AppKit calls for assistive technology, in process, so no TCC
// permission is needed. Covers structure, attributes, actions, modality, and
// the lifetime cases that crashed the per-query bridge.

#import <Cocoa/Cocoa.h>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <nk/platform/application.h>
#include <nk/platform/platform_backend.h>
#include <nk/platform/window.h>
#include <nk/widgets/button.h>
#include <nk/widgets/check_box.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/label.h>
#include <nk/widgets/text_field.h>

namespace {

class Column : public nk::Widget {
public:
    static std::shared_ptr<Column> create() { return std::shared_ptr<Column>(new Column()); }

    void append(std::shared_ptr<nk::Widget> child) { append_child(std::move(child)); }

    void remove(nk::Widget& child) { remove_child(child); }

    [[nodiscard]] nk::SizeRequest measure(const nk::Constraints& /*constraints*/) const override {
        return {200, 200, 200, 200};
    }

    void allocate(const nk::Rect& allocation) override {
        nk::Widget::allocate(allocation);
        float y = allocation.y;
        for (const auto& child : children()) {
            child->allocate({allocation.x, y, 160, 28});
            y += 32;
        }
    }

private:
    Column() = default;
};

NSView* root_element(nk::Window& window) {
    auto* ns_window = (__bridge NSWindow*)window.native_surface()->native_handle();
    return ns_window.contentView;
}

id find_element(NSArray* elements, NSString* role, NSString* label) {
    for (id element in elements) {
        if ([[element accessibilityRole] isEqualToString:role] &&
            (label == nil || [[element accessibilityLabel] isEqualToString:label])) {
            return element;
        }
    }
    return nil;
}

void settle(nk::Application& app) {
    for (int i = 0; i < 3; ++i) {
        (void)app.event_loop().poll();
    }
}

} // namespace

TEST_CASE("macOS bridge exposes widget elements with roles, labels, and values",
          "[accessibility][macos]") {
    @autoreleasepool {
        nk::Application app(0, nullptr);
        nk::Window window({.title = "Bridge", .width = 320, .height = 240});
        auto column = Column::create();
        auto title = nk::Label::create("Settings");
        auto save = nk::Button::create("Save");
        save->set_debug_name("save-button");
        auto check = nk::CheckBox::create("Mute");
        auto secret = nk::TextField::create("hunter2");
        secret->set_secure_text_entry(true);
        column->append(title);
        column->append(save);
        column->append(check);
        column->append(secret);
        window.set_child(column);
        window.present();
        settle(app);

        NSView* root = root_element(window);
        REQUIRE(root != nil);
        CHECK([root isAccessibilityElement]);
        CHECK([[root accessibilityLabel] isEqualToString:@"Bridge"]);
        NSArray* children = [root accessibilityChildren];
        REQUIRE(children.count == 4);
        CHECK([children isEqualToArray:[root accessibilityChildren]]);

        id label = find_element(children, NSAccessibilityStaticTextRole, nil);
        REQUIRE(label != nil);
        CHECK([[label accessibilityValue] isEqual:@"Settings"]);
        CHECK([label accessibilityParent] == root);

        id button = find_element(children, NSAccessibilityButtonRole, @"Save");
        REQUIRE(button != nil);
        CHECK([[button accessibilityIdentifier] isEqualToString:@"save-button"]);
        CHECK([button isAccessibilityEnabled]);
        CHECK([button isAccessibilitySelectorAllowed:@selector(accessibilityPerformPress)]);
        const NSRect frame = [button accessibilityFrame];
        CHECK(frame.size.width == 160);
        CHECK(frame.size.height == 28);
        CHECK([root accessibilityHitTest:NSMakePoint(NSMidX(frame), NSMidY(frame))] == button);

        id checkbox = find_element(children, NSAccessibilityCheckBoxRole, @"Mute");
        REQUIRE(checkbox != nil);
        CHECK([[checkbox accessibilityValue] isEqual:@0]);
        CHECK([checkbox accessibilityPerformPress]);
        CHECK([[checkbox accessibilityValue] isEqual:@1]);

        id field = find_element(children, NSAccessibilityTextFieldRole, nil);
        REQUIRE(field != nil);
        CHECK([[field accessibilityValue] isEqual:@"\u2022\u2022\u2022\u2022\u2022\u2022\u2022"]);
        [field setAccessibilityFocused:YES];
        CHECK([field isAccessibilityFocused]);
        CHECK([root accessibilityFocusedUIElement] == field);
        CHECK(NSEqualRanges([field accessibilitySelectedTextRange], NSMakeRange(7, 0)));
    }
}

TEST_CASE("macOS bridge routes presses and blocks disabled or covered controls",
          "[accessibility][macos]") {
    @autoreleasepool {
        nk::Application app(0, nullptr);
        nk::Window window({.title = "Actions", .width = 320, .height = 240});
        auto column = Column::create();
        auto save = nk::Button::create("Save");
        int saves = 0;
        auto connection = save->on_clicked().connect([&] { ++saves; });
        column->append(save);
        window.set_child(column);
        window.present();
        settle(app);

        NSView* root = root_element(window);
        id button = find_element([root accessibilityChildren], NSAccessibilityButtonRole, @"Save");
        REQUIRE(button != nil);
        CHECK([button accessibilityPerformPress]);
        CHECK(saves == 1);

        save->set_sensitive(false);
        CHECK_FALSE([button isAccessibilityEnabled]);
        CHECK_FALSE([button isAccessibilitySelectorAllowed:@selector(accessibilityPerformPress)]);
        CHECK_FALSE([button accessibilityPerformPress]);
        save->set_sensitive(true);

        auto dialog = nk::Dialog::create("Discard changes?");
        dialog->present(window);
        settle(app);
        NSArray* modal = [root accessibilityChildren];
        REQUIRE(modal.count == 1);
        CHECK([[modal[0] accessibilityRole] isEqualToString:NSAccessibilityGroupRole]);
        CHECK_FALSE([button isAccessibilityElement]);
        CHECK([button accessibilityParent] == nil);
        CHECK_FALSE([button accessibilityPerformPress]);
        CHECK(saves == 1);

        dialog->close();
        settle(app);
        CHECK([[root accessibilityChildren] containsObject:button]);
        CHECK([button accessibilityPerformPress]);
        CHECK(saves == 2);
        CHECK(connection.connected());
    }
}

TEST_CASE("macOS bridge proxies survive widget and window teardown", "[accessibility][macos]") {
    NSMutableArray* retained = [[NSMutableArray alloc] init];
    @autoreleasepool {
        nk::Application app(0, nullptr);
        {
            nk::Window window({.title = "Lifetime", .width = 320, .height = 240});
            auto column = Column::create();
            window.set_child(column);
            window.present();
            settle(app);
            NSView* root = root_element(window);

            // Repeated churn: create, query, destroy, and query the stale proxies.
            for (int round = 0; round < 200; ++round) {
                @autoreleasepool {
                    auto button = nk::Button::create("Transient");
                    column->append(button);
                    NSArray* children = [root accessibilityChildren];
                    REQUIRE(children.count == 1);
                    id element = children[0];
                    [retained addObject:element];
                    CHECK([[element accessibilityLabel] isEqualToString:@"Transient"]);
                    column->remove(*button);
                    button.reset();
                    CHECK([[root accessibilityChildren] count] == 0);
                    CHECK_FALSE([element isAccessibilityElement]);
                    CHECK([element accessibilityParent] == nil);
                    CHECK([[element accessibilityChildren] count] == 0);
                    CHECK_FALSE([element accessibilityPerformPress]);
                    CHECK([[element accessibilityRole] isEqualToString:NSAccessibilityUnknownRole]);
                }
            }

            auto survivor = nk::Button::create("Survivor");
            column->append(survivor);
            [retained addObject:[root accessibilityChildren][0]];
            settle(app);
        }
        // The window and its widgets are gone; AppKit may still hold every proxy.
        for (id element in retained) {
            CHECK_FALSE([element isAccessibilityElement]);
            CHECK([element accessibilityParent] == nil);
            CHECK([element accessibilityValue] == nil);
            CHECK_FALSE([element accessibilityPerformPress]);
            CHECK(NSEqualRects([element accessibilityFrame], NSZeroRect));
        }
    }
    [retained release];
}
