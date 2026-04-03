/// @file macos_backend.mm
/// @brief macOS Cocoa platform backend implementation.

#include "macos_backend.h"

#include "macos_window.h"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <nk/runtime/event_loop.h>
#include <optional>
#include <utility>

// ---------------------------------------------------------------------------
// NKAppDelegate — handles application-level events
// ---------------------------------------------------------------------------

@interface NKAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, assign) int exitCode;
@end

@implementation NKAppDelegate

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    // The nk event loop manages shutdown; let it decide.
    return NSTerminateCancel;
}

- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    // Post a dummy event so the custom NK loop can immediately consume any
    // queued Cocoa startup work.
    NSEvent* dummy = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSMakePoint(0, 0)
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [NSApp postEvent:dummy atStart:YES];
}

@end

namespace {

struct NativeMenuActionTargetState {
    std::function<void(std::string_view)>* handler = nullptr;
    std::string action_name;
};

} // namespace

@interface NKMenuActionTarget : NSObject
@property(nonatomic, assign) void* state;
- (void)activate:(id)sender;
@end

@implementation NKMenuActionTarget

- (void)activate:(__unused id)sender {
    auto* state = static_cast<NativeMenuActionTargetState*>(self.state);
    if (state == nullptr || state->handler == nullptr || !(*state->handler)) {
        return;
    }
    (*state->handler)(state->action_name);
}

@end

// ---------------------------------------------------------------------------
// MacosBackend::Impl
// ---------------------------------------------------------------------------

namespace nk {

struct MacosBackend::Impl {
    NKAppDelegate* delegate = nil;
    CFRunLoopTimerRef poll_timer = nullptr;
    EventLoop* current_loop = nullptr;
    int exit_code = 0;
    bool quit_requested = false;
    SystemPreferencesObserver system_preferences_observer;
    id appearance_observer = nil;
    id accessibility_observer = nil;
    std::function<void(std::string_view)> native_menu_action_handler;
    std::vector<std::unique_ptr<NativeMenuActionTargetState>> native_menu_target_states;
    NSMutableArray* native_menu_targets = nil;
    NSMenu* native_main_menu = nil;

    static void emit_preferences_change(Impl& impl);
};

namespace {

SystemPreferences query_system_preferences() {
    SystemPreferences preferences;
    preferences.platform_family = PlatformFamily::MacOS;
    preferences.desktop_environment = DesktopEnvironment::Other;
    preferences.transparency = TransparencyPreference::Allowed;

    @autoreleasepool {
        if (@available(macOS 10.14, *)) {
            auto best_match = [NSApp.effectiveAppearance bestMatchFromAppearancesWithNames:@[
                NSAppearanceNameAqua,
                NSAppearanceNameDarkAqua
            ]];
            if ([best_match isEqualToString:NSAppearanceNameDarkAqua]) {
                preferences.color_scheme = ColorScheme::Dark;
            } else {
                preferences.color_scheme = ColorScheme::Light;
            }

            NSColor* accent = [NSColor controlAccentColor];
            NSColor* srgb = [accent colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
            if (srgb != nil) {
                preferences.accent_color = Color{
                    static_cast<float>(srgb.redComponent),
                    static_cast<float>(srgb.greenComponent),
                    static_cast<float>(srgb.blueComponent),
                    1.0F,
                };
            }
        }

        NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
        if (workspace.accessibilityDisplayShouldIncreaseContrast) {
            preferences.contrast = ContrastPreference::High;
        }
        if (workspace.accessibilityDisplayShouldReduceMotion) {
            preferences.motion = MotionPreference::Reduced;
        }
        if (workspace.accessibilityDisplayShouldReduceTransparency) {
            preferences.transparency = TransparencyPreference::Reduced;
        }
    }

    return preferences;
}

std::optional<NSString*> native_menu_key_equivalent(KeyCode key) {
    switch (key) {
    case KeyCode::A:
    case KeyCode::B:
    case KeyCode::C:
    case KeyCode::D:
    case KeyCode::E:
    case KeyCode::F:
    case KeyCode::G:
    case KeyCode::H:
    case KeyCode::I:
    case KeyCode::J:
    case KeyCode::K:
    case KeyCode::L:
    case KeyCode::M:
    case KeyCode::N:
    case KeyCode::O:
    case KeyCode::P:
    case KeyCode::Q:
    case KeyCode::R:
    case KeyCode::S:
    case KeyCode::T:
    case KeyCode::U:
    case KeyCode::V:
    case KeyCode::W:
    case KeyCode::X:
    case KeyCode::Y:
    case KeyCode::Z: {
        const auto code =
            static_cast<char>('a' + (static_cast<int>(key) - static_cast<int>(KeyCode::A)));
        return [NSString stringWithFormat:@"%c", code];
    }
    case KeyCode::Num1:
        return @"1";
    case KeyCode::Num2:
        return @"2";
    case KeyCode::Num3:
        return @"3";
    case KeyCode::Num4:
        return @"4";
    case KeyCode::Num5:
        return @"5";
    case KeyCode::Num6:
        return @"6";
    case KeyCode::Num7:
        return @"7";
    case KeyCode::Num8:
        return @"8";
    case KeyCode::Num9:
        return @"9";
    case KeyCode::Num0:
        return @"0";
    case KeyCode::Return:
        return @"\r";
    case KeyCode::Space:
        return @" ";
    case KeyCode::Tab:
        return @"\t";
    case KeyCode::Comma:
        return @",";
    case KeyCode::Period:
        return @".";
    case KeyCode::Slash:
        return @"/";
    case KeyCode::Semicolon:
        return @";";
    case KeyCode::Apostrophe:
        return @"'";
    case KeyCode::Minus:
        return @"-";
    case KeyCode::Equals:
        return @"=";
    case KeyCode::LeftBracket:
        return @"[";
    case KeyCode::RightBracket:
        return @"]";
    case KeyCode::Backslash:
        return @"\\";
    default:
        return std::nullopt;
    }
}

NSEventModifierFlags native_menu_modifier_flags(NativeMenuModifier modifiers) {
    NSEventModifierFlags flags = 0;
    if ((modifiers & NativeMenuModifier::Shift) != NativeMenuModifier::None) {
        flags |= NSEventModifierFlagShift;
    }
    if ((modifiers & NativeMenuModifier::Ctrl) != NativeMenuModifier::None) {
        flags |= NSEventModifierFlagControl;
    }
    if ((modifiers & NativeMenuModifier::Alt) != NativeMenuModifier::None) {
        flags |= NSEventModifierFlagOption;
    }
    if ((modifiers & NativeMenuModifier::Super) != NativeMenuModifier::None) {
        flags |= NSEventModifierFlagCommand;
    }
    return flags;
}

void populate_native_submenu(
    NSMenu* menu,
    std::span<const NativeMenuItem> items,
    std::function<void(std::string_view)>& action_handler,
    std::vector<std::unique_ptr<NativeMenuActionTargetState>>& target_states,
    NSMutableArray* targets) {
    for (const auto& item : items) {
        if (item.separator) {
            [menu addItem:[NSMenuItem separatorItem]];
            continue;
        }

        NSString* title = [NSString stringWithUTF8String:item.label.c_str()];
        if (!item.children.empty()) {
            NSMenuItem* submenu_item = [[NSMenuItem alloc] initWithTitle:title
                                                                  action:nil
                                                           keyEquivalent:@""];
            [submenu_item setEnabled:item.enabled];
            NSMenu* submenu = [[NSMenu alloc] initWithTitle:title];
            populate_native_submenu(submenu, item.children, action_handler, target_states, targets);
            [menu addItem:submenu_item];
            [menu setSubmenu:submenu forItem:submenu_item];
            continue;
        }

        NSString* key_equivalent = @"";
        NSEventModifierFlags modifier_flags = 0;
        if (item.shortcut.has_value()) {
            if (const auto key = native_menu_key_equivalent(item.shortcut->key); key.has_value()) {
                key_equivalent = *key;
                modifier_flags = native_menu_modifier_flags(item.shortcut->modifiers);
            }
        }

        NSMenuItem* menu_item = [[NSMenuItem alloc] initWithTitle:title
                                                           action:nil
                                                    keyEquivalent:key_equivalent];
        [menu_item setEnabled:item.enabled && !item.action_name.empty()];
        [menu_item setKeyEquivalentModifierMask:modifier_flags];

        if (!item.action_name.empty()) {
            auto state = std::make_unique<NativeMenuActionTargetState>();
            state->handler = &action_handler;
            state->action_name = item.action_name;

            NKMenuActionTarget* target = [[NKMenuActionTarget alloc] init];
            target.state = state.get();
            [menu_item setTarget:target];
            [menu_item setAction:@selector(activate:)];
            [targets addObject:target];
            target_states.push_back(std::move(state));
        }

        [menu addItem:menu_item];
    }
}

} // namespace

void MacosBackend::Impl::emit_preferences_change(Impl& impl) {
    if (!impl.system_preferences_observer) {
        return;
    }
    impl.system_preferences_observer(query_system_preferences());
}

MacosBackend::MacosBackend() : impl_(std::make_unique<Impl>()) {}

MacosBackend::~MacosBackend() {
    shutdown();
}

Result<void> MacosBackend::initialize() {
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        impl_->delegate = [[NKAppDelegate alloc] init];
        [NSApp setDelegate:impl_->delegate];
        [NSApp finishLaunching];
    }
    return {};
}

void MacosBackend::shutdown() {
    @autoreleasepool {
        [NSApp setMainMenu:nil];
        impl_->native_main_menu = nil;
        impl_->native_menu_targets = nil;
        impl_->native_menu_target_states.clear();
        impl_->native_menu_action_handler = {};
        stop_system_preferences_observation();
        if (impl_->poll_timer) {
            CFRunLoopTimerInvalidate(impl_->poll_timer);
            CFRelease(impl_->poll_timer);
            impl_->poll_timer = nullptr;
        }
        if (impl_->delegate) {
            [NSApp setDelegate:nil];
            impl_->delegate = nil;
        }
    }
}

std::unique_ptr<NativeSurface> MacosBackend::create_surface(const WindowConfig& config,
                                                            Window& owner) {
    @autoreleasepool {
        return std::make_unique<MacosSurface>(config, owner);
    }
}

int MacosBackend::run_event_loop(EventLoop& loop) {
    impl_->current_loop = &loop;
    impl_->quit_requested = false;
    impl_->exit_code = 0;

    // Create a repeating timer at ~120 Hz to drive the NK event loop.
    CFTimeInterval interval = 1.0 / 120.0;
    CFRunLoopTimerContext ctx{};
    ctx.info = impl_.get();

    impl_->poll_timer = CFRunLoopTimerCreate(
        kCFAllocatorDefault,
        CFAbsoluteTimeGetCurrent() + interval,
        interval,
        0, // flags
        0, // order
        [](CFRunLoopTimerRef /*timer*/, void* info) {
            auto* impl = static_cast<MacosBackend::Impl*>(info);
            if (impl->current_loop) {
                impl->current_loop->poll();
            }
            if (impl->quit_requested) {
                CFRunLoopStop(CFRunLoopGetMain());
            }
        },
        &ctx);

    CFRunLoopAddTimer(CFRunLoopGetMain(), impl_->poll_timer, kCFRunLoopCommonModes);

    // Enter the Cocoa run loop.
    while (!impl_->quit_requested) {
        @autoreleasepool {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantFuture]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event) {
                [NSApp sendEvent:event];
                [NSApp updateWindows];
            }
        }
    }

    // Clean up the timer.
    if (impl_->poll_timer) {
        CFRunLoopTimerInvalidate(impl_->poll_timer);
        CFRelease(impl_->poll_timer);
        impl_->poll_timer = nullptr;
    }

    impl_->current_loop = nullptr;
    return impl_->exit_code;
}

void MacosBackend::wake_event_loop() {
    CFRunLoopWakeUp(CFRunLoopGetMain());

    // Also post a dummy event so that nextEventMatchingMask returns.
    @autoreleasepool {
        NSEvent* dummy = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                            location:NSMakePoint(0, 0)
                                       modifierFlags:0
                                           timestamp:0
                                        windowNumber:0
                                             context:nil
                                             subtype:0
                                               data1:0
                                               data2:0];
        [NSApp postEvent:dummy atStart:YES];
    }
}

void MacosBackend::request_quit(int exit_code) {
    impl_->exit_code = exit_code;
    impl_->quit_requested = true;
    wake_event_loop();
}

bool MacosBackend::supports_open_file_dialog() const {
    return true;
}

OpenFileDialogResult MacosBackend::show_open_file_dialog(std::string_view title,
                                                         const std::vector<std::string>& filters) {
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        [panel setTitle:[NSString stringWithUTF8String:std::string(title).c_str()]];

        if (!filters.empty()) {
            NSMutableArray<NSString*>* types = [NSMutableArray array];
            for (const auto& ext : filters) {
                // Strip leading "*." if present.
                std::string_view sv = ext;
                if (sv.starts_with("*.")) {
                    sv = sv.substr(2);
                } else if (sv.starts_with(".")) {
                    sv = sv.substr(1);
                }
                [types addObject:[NSString stringWithUTF8String:std::string(sv).c_str()]];
            }
            if (@available(macOS 11.0, *)) {
                NSMutableArray<UTType*>* content_types = [NSMutableArray array];
                for (NSString* ext in types) {
                    UTType* ut = [UTType typeWithFilenameExtension:ext];
                    if (ut) {
                        [content_types addObject:ut];
                    }
                }
                if (content_types.count > 0) {
                    [panel setAllowedContentTypes:content_types];
                }
            } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
                [panel setAllowedFileTypes:types];
#pragma clang diagnostic pop
            }
        }

        NSModalResponse response = [panel runModal];
        if (response == NSModalResponseOK && panel.URL) {
            return std::string(panel.URL.path.UTF8String);
        }
        return Unexpected(FileDialogError::Cancelled);
    }
}

bool MacosBackend::supports_clipboard_text() const {
    return true;
}

std::string MacosBackend::clipboard_text() const {
    @autoreleasepool {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        NSString* value = [pasteboard stringForType:NSPasteboardTypeString];
        if (value == nil) {
            return {};
        }
        return std::string(value.UTF8String);
    }
}

void MacosBackend::set_clipboard_text(std::string_view text) {
    @autoreleasepool {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];
        NSString* value = [[NSString alloc] initWithBytes:text.data()
                                                   length:text.size()
                                                 encoding:NSUTF8StringEncoding];
        if (value != nil) {
            [pasteboard setString:value forType:NSPasteboardTypeString];
        }
    }
}

bool MacosBackend::supports_native_app_menu() const {
    return true;
}

void MacosBackend::set_native_app_menu(std::span<const NativeMenu> menus,
                                       NativeMenuActionHandler action_handler) {
    @autoreleasepool {
        impl_->native_menu_action_handler = std::move(action_handler);
        impl_->native_menu_target_states.clear();
        impl_->native_menu_targets = [[NSMutableArray alloc] init];

        NSMenu* main_menu = [[NSMenu alloc] initWithTitle:@""];
        for (const auto& menu : menus) {
            NSString* title = [NSString stringWithUTF8String:menu.title.c_str()];
            NSMenuItem* top_level = [[NSMenuItem alloc] initWithTitle:title
                                                               action:nil
                                                        keyEquivalent:@""];
            NSMenu* submenu = [[NSMenu alloc] initWithTitle:title];
            populate_native_submenu(submenu,
                                    menu.items,
                                    impl_->native_menu_action_handler,
                                    impl_->native_menu_target_states,
                                    impl_->native_menu_targets);
            [main_menu addItem:top_level];
            [main_menu setSubmenu:submenu forItem:top_level];
        }

        impl_->native_main_menu = main_menu;
        [NSApp setMainMenu:main_menu];
    }
}

SystemPreferences MacosBackend::system_preferences() const {
    return query_system_preferences();
}

bool MacosBackend::supports_system_preferences_observation() const {
    return true;
}

void MacosBackend::start_system_preferences_observation(SystemPreferencesObserver observer) {
    stop_system_preferences_observation();

    impl_->system_preferences_observer = std::move(observer);
    if (!impl_->system_preferences_observer) {
        return;
    }

    auto* impl = impl_.get();

    impl_->appearance_observer = [[NSDistributedNotificationCenter defaultCenter]
        addObserverForName:@"AppleInterfaceThemeChangedNotification"
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification* notification) {
                  Impl::emit_preferences_change(*impl);
                }];

    impl_->accessibility_observer = [[[NSWorkspace sharedWorkspace] notificationCenter]
        addObserverForName:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                    object:nil
                     queue:[NSOperationQueue mainQueue]
                usingBlock:^(__unused NSNotification* notification) {
                  Impl::emit_preferences_change(*impl);
                }];
}

void MacosBackend::stop_system_preferences_observation() {
    @autoreleasepool {
        if (impl_->appearance_observer != nil) {
            [[NSDistributedNotificationCenter defaultCenter]
                removeObserver:impl_->appearance_observer];
            impl_->appearance_observer = nil;
        }
        if (impl_->accessibility_observer != nil) {
            [[[NSWorkspace sharedWorkspace] notificationCenter]
                removeObserver:impl_->accessibility_observer];
            impl_->accessibility_observer = nil;
        }
    }

    impl_->system_preferences_observer = {};
}

} // namespace nk
