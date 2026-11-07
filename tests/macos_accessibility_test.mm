// Drives the macOS accessibility bridge through the same NSAccessibility
// methods AppKit calls for assistive technology, in process, so no TCC
// permission is needed. Covers structure, attributes, actions, modality, and
// the lifetime cases that crashed the per-query bridge.

#include "../examples/showcase/showcase_app.h"

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/application.h>
#include <nk/platform/native_menu.h>
#include <nk/platform/native_toolbar.h>
#include <nk/platform/platform_backend.h>
#include <nk/platform/window.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/check_box.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/label.h>
#include <nk/widgets/list_view.h>
#include <nk/widgets/text_field.h>
#import <objc/runtime.h>
#include <string>
#include <vector>

@interface NKSurfaceLifetimeProbe : NSObject {
@public
    std::shared_ptr<std::size_t> destructions;
}
@end

@implementation NKSurfaceLifetimeProbe

- (void)dealloc {
    ++*destructions;
    [super dealloc];
}

@end

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

void settle_native(nk::Application& app) {
    @autoreleasepool {
        settle(app);
        NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                            untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                               inMode:NSDefaultRunLoopMode
                                              dequeue:YES];
        if (event != nil) {
            [NSApp sendEvent:event];
        }
        [NSApp updateWindows];
    }
}

void observe_destruction(id object, const std::shared_ptr<std::size_t>& counter) {
    REQUIRE(object != nil);
    auto* probe = [[NKSurfaceLifetimeProbe alloc] init];
    probe->destructions = counter;
    static char association_key;
    objc_setAssociatedObject(object, &association_key, probe, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [probe release];
}

} // namespace

TEST_CASE("macOS surface releases native objects after window teardown", "[macos][memory]") {
    @autoreleasepool {
        nk::Application app(0, nullptr);
        std::array<std::shared_ptr<std::size_t>, 4> counters;
        for (auto& counter : counters) {
            counter = std::make_shared<std::size_t>(0);
        }
        @autoreleasepool {
            auto window =
                std::make_unique<nk::Window>(nk::WindowConfig{.title = "Surface lifetime"});
            window->set_child(nk::Label::create("Window lifetime"));
            window->present();
            settle(app);
            auto* native = (__bridge NSWindow*)window->native_surface()->native_handle();
            observe_destruction(native, counters[0]);
            observe_destruction(native.contentView, counters[1]);
            observe_destruction(native.delegate, counters[2]);
            REQUIRE(native.contentView.trackingAreas.count > 0);
            observe_destruction(native.contentView.trackingAreas[0], counters[3]);
            window.reset();
            settle(app);
        }
        settle(app);
        for (const auto& counter : counters) {
            CHECK(*counter == 1);
        }
    }
}

TEST_CASE("macOS view releases replaced tracking areas", "[macos][memory]") {
    @autoreleasepool {
        nk::Application app(0, nullptr);
        nk::Window window({.title = "Tracking lifetime"});
        window.set_child(nk::Label::create("Tracking"));
        window.present();
        settle(app);
        auto* native = (__bridge NSWindow*)window.native_surface()->native_handle();
        auto destructions = std::make_shared<std::size_t>(0);
        for (std::size_t iteration = 0; iteration < 5; ++iteration) {
            @autoreleasepool {
                REQUIRE(native.contentView.trackingAreas.count > 0);
                observe_destruction(native.contentView.trackingAreas[0], destructions);
                [native.contentView updateTrackingAreas];
                settle(app);
            }
            CHECK(*destructions == iteration + 1);
        }
    }
}

TEST_CASE("macOS toolbar replacement and removal release objects and disconnect callbacks",
          "[macos][memory][toolbar]") {
    bool edit_search = false;
    SECTION("idle toolbar") {}
    SECTION("search field being edited") {
        edit_search = true;
    }
    @autoreleasepool {
        nk::Application app(0, nullptr);

        struct ObservedObject {
            std::string label;
            std::shared_ptr<std::size_t> destructions;
        };

        std::vector<ObservedObject> observations;
        @autoreleasepool {
            auto window =
                std::make_unique<nk::Window>(nk::WindowConfig{.title = "Toolbar lifetime"});
            window->set_child(nk::Label::create("Toolbar content"));
            window->present();
            settle_native(app);
            auto* native = (__bridge NSWindow*)window->native_surface()->native_handle();
            const auto config = [](int generation) {
                nk::NativeToolbarConfig result;
                result.identifier =
                    "org.nodalkit.test.toolbar-lifetime." + std::to_string(generation);
                result.allows_user_customization = false;
                result.items = {{.kind = nk::NativeToolbarItem::Kind::Button,
                                 .identifier = "button",
                                 .label = "Button",
                                 .tooltip = {},
                                 .symbol_name = {},
                                 .on_activate = {},
                                 .on_search_submit = {}},
                                {.kind = nk::NativeToolbarItem::Kind::SearchField,
                                 .identifier = "search",
                                 .label = "Search",
                                 .tooltip = {},
                                 .symbol_name = {},
                                 .on_activate = {},
                                 .on_search_submit = {}}};
                return result;
            };
            const auto observe_toolbar = [&](int generation,
                                             bool observe_delegate) -> NSSearchField* {
                const auto observe = [&](id object) {
                    auto destructions = std::make_shared<std::size_t>(0);
                    observe_destruction(object, destructions);
                    observations.push_back(
                        {std::to_string(generation) + ": " + object_getClassName(object),
                         std::move(destructions)});
                };
                NSToolbar* toolbar = native.toolbar;
                observe(toolbar);
                if (observe_delegate) {
                    observe(toolbar.delegate);
                }
                NSSearchField* search = nil;
                REQUIRE(toolbar.items.count == 2);
                for (NSToolbarItem* item in toolbar.items) {
                    observe(item);
                    if ([item isKindOfClass:[NSSearchToolbarItem class]]) {
                        auto* search_item = static_cast<NSSearchToolbarItem*>(item);
                        if (edit_search) {
                            [search_item beginSearchInteraction];
                        }
                        search = search_item.searchField;
                    }
                }
                observe(search);
                if (edit_search) {
                    const auto deadline =
                        std::chrono::steady_clock::now() + std::chrono::seconds(1);
                    do {
                        settle_native(app);
                        [native makeFirstResponder:search];
                        [search selectText:nil];
                    } while (search.currentEditor == nil &&
                             std::chrono::steady_clock::now() < deadline);
                    INFO("generation="
                         << generation << " active=" << NSApp.active
                         << " key=" << native.isKeyWindow << " visible=" << native.visible
                         << " field window=" << search.window << " field hidden=" << search.hidden
                         << " first responder=" << object_getClassName(native.firstResponder));
                    REQUIRE(search.currentEditor != nil);
                }
                return [search retain];
            };
            @autoreleasepool {
                window->set_native_toolbar(config(0));
                settle_native(app);
                NSSearchField* search = observe_toolbar(0, true);
                window->set_native_toolbar(config(1));
                settle_native(app);
                CHECK(search.delegate == nil);
                CHECK(search.target == nil);
                CHECK(search.action == nil);
                [search release];
            }
            settle_native(app);
            @autoreleasepool {
                NSSearchField* search = observe_toolbar(1, false);
                window->clear_native_toolbar();
                settle_native(app);
                CHECK(search.delegate == nil);
                CHECK(search.target == nil);
                CHECK(search.action == nil);
                [search release];
            }
            settle_native(app);
            @autoreleasepool {
                window->set_native_toolbar(config(2));
                settle_native(app);
                [observe_toolbar(2, true) release];
                window.reset();
                settle_native(app);
            }
            settle_native(app);
        }
        settle_native(app);
        REQUIRE(observations.size() == 14);
        const auto all_destroyed = [&] {
            return std::ranges::all_of(
                observations, [](const auto& object) { return *object.destructions == 1; });
        };
        // Retired toolbar animations finish through AppKit's event processing,
        // not just the toolkit poll or a bare NSRunLoop wait.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!all_destroyed() && std::chrono::steady_clock::now() < deadline) {
            settle_native(app);
        }
        for (const auto& object : observations) {
            INFO("toolbar generation and object: " << object.label);
            CHECK(*object.destructions == 1);
        }
    }
}

TEST_CASE("macOS secure input follows focus, field mode, hiding, and teardown", "[macos][secure]") {
    @autoreleasepool {
        nk::Application app(0, nullptr);
        if (IsSecureEventInputEnabled()) {
            SKIP("Another input client already holds secure event input");
        }
        auto window = std::make_unique<nk::Window>(nk::WindowConfig{.title = "Secure input"});
        auto box = nk::Box::create(nk::Orientation::Vertical);
        auto secret = nk::TextField::create("secret");
        auto ordinary = nk::TextField::create();
        secret->set_secure_text_entry(true);
        box->append(secret);
        box->append(ordinary);
        window->set_child(box);
        window->present();
        settle(app);
        auto* native = (__bridge NSWindow*)window->native_surface()->native_handle();
        [native makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        for (int attempt = 0; attempt < 50 && !NSApp.active; ++attempt) {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event != nil) {
                [NSApp sendEvent:event];
            }
        }
        [native makeFirstResponder:native.contentView];
        secret->grab_focus();
        settle(app);
        REQUIRE(NSApp.active);
        REQUIRE(native.isKeyWindow);
        REQUIRE(IsSecureEventInputEnabled());
        CHECK([native.contentView inputContext] == nil);

        NSEvent* typed = [NSEvent keyEventWithType:NSEventTypeKeyDown
                                          location:NSZeroPoint
                                     modifierFlags:0
                                         timestamp:0
                                      windowNumber:native.windowNumber
                                           context:nil
                                        characters:@"x"
                       charactersIgnoringModifiers:@"x"
                                         isARepeat:NO
                                           keyCode:kVK_ANSI_X];
        [native.contentView keyDown:typed];
        CHECK(secret->text() == "secretx");
        ordinary->grab_focus();
        CHECK_FALSE(IsSecureEventInputEnabled());
        CHECK([native.contentView inputContext] != nil);
        [native.contentView setMarkedText:@"pending"
                            selectedRange:NSMakeRange(7, 0)
                         replacementRange:NSMakeRange(NSNotFound, 0)];
        REQUIRE([native.contentView hasMarkedText]);
        ordinary->set_secure_text_entry(true);
        CHECK_FALSE([native.contentView hasMarkedText]);
        [native.contentView unmarkText];
        CHECK(ordinary->text().empty());
        ordinary->set_secure_text_entry(false);
        secret->grab_focus();
        REQUIRE(IsSecureEventInputEnabled());
        secret->set_secure_text_entry(false);
        CHECK_FALSE(IsSecureEventInputEnabled());
        secret->set_secure_text_entry(true);
        REQUIRE(IsSecureEventInputEnabled());
        secret->set_editable(false);
        CHECK_FALSE(IsSecureEventInputEnabled());
        secret->set_editable(true);
        REQUIRE(IsSecureEventInputEnabled());
        [native makeFirstResponder:nil];
        CHECK_FALSE(IsSecureEventInputEnabled());
        [native makeFirstResponder:native.contentView];
        window->native_surface()->text_input_state_changed();
        REQUIRE(IsSecureEventInputEnabled());
        window->hide();
        CHECK_FALSE(IsSecureEventInputEnabled());
        window->present();
        [native makeKeyAndOrderFront:nil];
        [native makeFirstResponder:native.contentView];
        secret->grab_focus();
        window->native_surface()->text_input_state_changed();
        REQUIRE(IsSecureEventInputEnabled());
        window.reset();
        CHECK_FALSE(IsSecureEventInputEnabled());
    }
}

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
        dialog->add_button("Keep", nk::DialogResponse::Cancel);
        auto response = nk::DialogResponse::None;
        auto response_connection =
            dialog->on_response().connect([&](nk::DialogResponse value) { response = value; });
        dialog->present(window);
        settle(app);
        NSArray* modal = [root accessibilityChildren];
        REQUIRE(modal.count == 1);
        CHECK([[modal[0] accessibilityRole] isEqualToString:NSAccessibilityGroupRole]);
        CHECK_FALSE([button isAccessibilityElement]);
        CHECK([button accessibilityParent] == nil);
        CHECK_FALSE([button accessibilityPerformPress]);
        CHECK(saves == 1);

        // The dialog's own response button is reachable and closes it.
        id keep =
            find_element([modal[0] accessibilityChildren], NSAccessibilityButtonRole, @"Keep");
        REQUIRE(keep != nil);
        CHECK([root accessibilityFocusedUIElement] == keep);
        CHECK([keep accessibilityPerformPress]);
        CHECK(response == nk::DialogResponse::Cancel);
        CHECK_FALSE(dialog->is_presented());
        CHECK(response_connection.connected());
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

TEST_CASE("macOS native Edit roles edit the active toolkit or native responder",
          "[macos][menu][editing]") {
    @autoreleasepool {
        nk::Application app(0, nullptr);
        nk::Window window({.title = "Native commands", .width = 500, .height = 280});
        auto field = nk::TextField::create("clipboard-test");
        window.set_child(field);
        window.present();
        settle(app);
        auto* native = (__bridge NSWindow*)window.native_surface()->native_handle();
        [native makeKeyAndOrderFront:nil];
        [native makeFirstResponder:root_element(window)];
        field->grab_focus();
        std::vector<nk::NativeMenuItem> items;
        for (const auto& [title, role] : std::vector<std::pair<std::string, nk::NativeMenuRole>>{
                 {"Cut", nk::NativeMenuRole::Cut},
                 {"Copy", nk::NativeMenuRole::Copy},
                 {"Paste", nk::NativeMenuRole::Paste},
                 {"Undo", nk::NativeMenuRole::Undo},
                 {"Redo", nk::NativeMenuRole::Redo},
                 {"Select All", nk::NativeMenuRole::SelectAll}}) {
            auto item = nk::NativeMenuItem::action(title, "application.custom-name");
            item.native_role = role;
            items.push_back(std::move(item));
        }
        auto disabled = nk::NativeMenuItem::action("Disabled Cut", "unused");
        disabled.native_role = nk::NativeMenuRole::Cut;
        disabled.enabled = false;
        items.push_back(std::move(disabled));
        app.set_native_app_menu({{"Edit", std::move(items)}});
        int callbacks = 0;
        auto connection =
            app.on_native_app_menu_action().connect([&](std::string_view) { ++callbacks; });
        NSMenu* menu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
        [menu update];
        CHECK_FALSE([[menu itemWithTitle:@"Disabled Cut"] isEnabled]);
        CHECK([[menu itemWithTitle:@"Disabled Cut"] action] == nil);
        const auto activate = [&](NSString* title) {
            NSMenuItem* item = [menu itemWithTitle:title];
            REQUIRE(item != nil);
            CHECK([item target] == nil);
            // Test processes need not be the OS key application. Start at this
            // window's real responder rather than NSApp's global key window.
            CHECK([[native firstResponder] tryToPerform:[item action] with:item]);
        };
        activate(@"Select All");
        activate(@"Copy");
        activate(@"Cut");
        CHECK(field->text().empty());
        activate(@"Undo");
        CHECK(field->text() == "clipboard-test");
        activate(@"Redo");
        CHECK(field->text().empty());
        activate(@"Paste");
        CHECK(field->text() == "clipboard-test");
        CHECK(callbacks == 0);

        NSSearchField* search = [[NSSearchField alloc] initWithFrame:NSMakeRect(0, 0, 180, 28)];
        [[native contentView] addSubview:search];
        [search setStringValue:@"native-search"];
        REQUIRE([native makeFirstResponder:search]);
        auto* editor = [search currentEditor];
        REQUIRE(editor != nil);
        activate(@"Select All");
        activate(@"Cut");
        CHECK([[editor string] length] == 0);
        CHECK(field->text() == "clipboard-test");
        activate(@"Paste");
        CHECK([[editor string] isEqualToString:@"native-search"]);
        [search removeFromSuperview];
        [search release];
    }
}

TEST_CASE("macOS toolbar callbacks respect modal sheets and resume after dismissal",
          "[macos][toolbar][modal]") {
    @autoreleasepool {
        nk::Application app(0, nullptr);
        nk::Window window({.title = "Toolbar modality", .width = 500, .height = 280});
        window.set_child(nk::Button::create("Content"));
        int clicks = 0;
        int submissions = 0;
        nk::NativeToolbarConfig config;
        config.identifier = "org.nodalkit.test.modal-toolbar";
        config.allows_user_customization = false;
        config.items = {
            {.kind = nk::NativeToolbarItem::Kind::Button,
             .identifier = "add",
             .label = "Add",
             .tooltip = {},
             .symbol_name = {},
             .on_activate = [&] { ++clicks; },
             .on_search_submit = {}},
            {.kind = nk::NativeToolbarItem::Kind::SearchField,
             .identifier = "search",
             .label = "Search",
             .tooltip = {},
             .symbol_name = {},
             .on_activate = {},
             .on_search_submit = [&](std::string_view) { ++submissions; }},
        };
        window.set_native_toolbar(std::move(config));
        window.present();
        settle(app);
        auto* native = (__bridge NSWindow*)window.native_surface()->native_handle();
        NSToolbarItem* button = nil;
        NSSearchField* search = nil;
        for (NSToolbarItem* item in native.toolbar.items) {
            if ([[item itemIdentifier] isEqualToString:@"add"]) {
                button = item;
            }
            if ([item isKindOfClass:[NSSearchToolbarItem class]]) {
                search = [static_cast<NSSearchToolbarItem*>(item) searchField];
            }
        }
        REQUIRE(button != nil);
        REQUIRE(search != nil);
        const auto activate = [&] {
            CHECK([NSApp sendAction:button.action to:button.target from:button]);
            CHECK([NSApp sendAction:search.action to:search.target from:search]);
        };
        activate();
        CHECK(clicks == 1);
        CHECK(submissions == 1);
        REQUIRE([native makeFirstResponder:search]);
        auto dialog = nk::Dialog::create("Preferences");
        dialog->set_presentation_style(nk::DialogPresentationStyle::Sheet);
        dialog->add_button("Later", nk::DialogResponse::Cancel);
        dialog->present(window);
        settle(app);
        CHECK_FALSE(button.isEnabled);
        CHECK_FALSE(search.isEnabled);
        CHECK([native firstResponder] == root_element(window));
        activate();
        CHECK(clicks == 1);
        CHECK(submissions == 1);
        id panel = [[root_element(window) accessibilityChildren] firstObject];
        id later = find_element([panel accessibilityChildren], NSAccessibilityButtonRole, @"Later");
        REQUIRE(later != nil);
        CHECK([later accessibilityPerformPress]);
        CHECK(button.isEnabled);
        CHECK(search.isEnabled);
        activate();
        CHECK(clicks == 2);
        CHECK(submissions == 2);
    }
}

TEST_CASE("macOS exposes the list's current item and activation", "[macos][accessibility][list]") {
    @autoreleasepool {
        nk::Application app(0, nullptr);
        nk::Window window({.title = "Accessible list", .width = 320, .height = 240});
        auto list = nk::ListView::create();
        auto model =
            std::make_shared<nk::StringListModel>(std::vector<std::string>{"Alpha", "Bravo"});
        auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
        selection->select(0);
        list->set_model(model);
        list->set_selection_model(selection);
        window.set_child(list);
        window.present();
        settle(app);
        id element = find_element(
            [root_element(window) accessibilityChildren], NSAccessibilityListRole, @"List view");
        REQUIRE(element != nil);
        CHECK([[element accessibilityValue] isEqualToString:@"Alpha, item 1 of 2"]);
        selection->set_current_row(1);
        selection->select(1);
        CHECK([[element accessibilityValue] isEqualToString:@"Bravo, item 2 of 2"]);
        std::size_t activated = 99;
        auto connection =
            list->on_row_activated().connect([&](std::size_t row) { activated = row; });
        CHECK([element accessibilityPerformPress]);
        CHECK(activated == 1);
    }
}

TEST_CASE("macOS native Window roles use AppKit window operations", "[macos][menu][window]") {
    @autoreleasepool {
        nk::Application app(0, nullptr);
        nk::Window window({.title = "Native window commands", .width = 320, .height = 240});
        window.set_child(nk::Button::create("Content"));
        window.present();
        settle(app);
        auto* native = (__bridge NSWindow*)window.native_surface()->native_handle();
        auto minimize = nk::NativeMenuItem::action("Minimize", "unused.minimize");
        minimize.native_role = nk::NativeMenuRole::Minimize;
        auto front = nk::NativeMenuItem::action("Bring All to Front", "unused.front");
        front.native_role = nk::NativeMenuRole::BringAllToFront;
        app.set_native_app_menu({{"Window", {minimize, front}}});
        NSMenu* menu = [[[NSApp mainMenu] itemAtIndex:0] submenu];
        NSMenuItem* minimize_item = [menu itemWithTitle:@"Minimize"];
        REQUIRE(minimize_item != nil);
        CHECK([minimize_item target] == nil);
        CHECK([native tryToPerform:[minimize_item action] with:minimize_item]);
        const auto minimized_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!native.isMiniaturized && std::chrono::steady_clock::now() < minimized_deadline) {
            (void)app.event_loop().poll();
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
        CHECK(native.isMiniaturized);
        [native deminiaturize:nil];
        const auto restored_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (native.isMiniaturized && std::chrono::steady_clock::now() < restored_deadline) {
            (void)app.event_loop().poll();
            [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                     beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
        }
        CHECK_FALSE(native.isMiniaturized);
        NSMenuItem* front_item = [menu itemWithTitle:@"Bring All to Front"];
        REQUIRE(front_item != nil);
        CHECK([front_item target] == nil);
        CHECK([NSApp tryToPerform:[front_item action] with:front_item]);
    }
}

TEST_CASE("The real showcase palette Quit command exits the application",
          "[macos][showcase][commands]") {
    @autoreleasepool {
        __block bool activated = false;
        __block int attempts = 0;
        NSTimer* timer = [NSTimer
            scheduledTimerWithTimeInterval:0.01
                                   repeats:YES
                                     block:^(NSTimer* timer) {
                                       ++attempts;
                                       for (NSWindow* native in [NSApp windows]) {
                                           if (![[native title]
                                                   isEqualToString:@"NodalKit Showcase"] ||
                                               !native.isVisible) {
                                               continue;
                                           }
                                           id palette = find_element(
                                               [[native contentView] accessibilityChildren],
                                               NSAccessibilityGroupRole,
                                               @"Command palette");
                                           if (palette != nil) {
                                               activated = [palette accessibilityPerformPress];
                                               [timer invalidate];
                                               return;
                                           }
                                       }
                                       if (attempts >= 200) {
                                           if (auto* app = nk::Application::instance()) {
                                               app->quit(74);
                                           }
                                           [timer invalidate];
                                       }
                                     }];
        const int result = run_showcase(0, nullptr);
        [timer invalidate];
        CHECK(activated);
        CHECK(result == 0);
    }
}
