/// @file macos_backend.mm
/// @brief macOS Cocoa platform backend implementation.

#include "macos_backend.h"

#include "macos_spell_checker.h"
#include "macos_window.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CADisplayLink.h>
#import <QuartzCore/CAMediaTiming.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <map>
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

API_AVAILABLE(macos(14.0))
@interface NKDisplayPollTarget : NSObject {
@public
    nk::EventLoop* loop;
    NSWindow* window;
    CFTimeInterval last_tick;
    CFTimeInterval interval;
    CFTimeInterval last_request;
    bool polling;
    std::vector<std::function<void()>> frames;
    std::vector<std::function<void()>> dispatching;
}
- (void)displayTick:(CADisplayLink*)sender;
- (void)flushFrames;
@end

@implementation NKDisplayPollTarget

- (void)displayTick:(CADisplayLink*)sender {
    @autoreleasepool {
        [[self retain] autorelease];
        if (loop == nullptr) {
            return;
        }
        const float rate = static_cast<float>(window.screen.maximumFramesPerSecond);
        if (rate > 0 && sender.preferredFrameRateRange.maximum != rate) {
            sender.preferredFrameRateRange = CAFrameRateRangeMake(rate, rate, rate);
        }
        last_tick = CACurrentMediaTime();
        interval = sender.duration > 0 ? sender.duration : 1.0 / 120.0;
        [self flushFrames];
        // Do not keep a display clock active for an otherwise static window.
        if (frames.empty() && CACurrentMediaTime() - last_request > 0.1) {
            sender.paused = YES;
        }
    }
}

- (void)flushFrames {
    if (loop == nullptr || polling || frames.empty()) {
        return;
    }
    [[self retain] autorelease];
    polling = true;
    dispatching.swap(frames);
    for (auto& frame : dispatching) {
        (void)loop->post(std::move(frame), "window.request-frame");
    }
    dispatching.clear();
    loop->poll();
    polling = false;
}

@end

namespace {

SEL native_menu_selector(nk::NativeMenuRole role) {
    switch (role) {
    case nk::NativeMenuRole::Undo:
        return @selector(undo:);
    case nk::NativeMenuRole::Redo:
        return @selector(redo:);
    case nk::NativeMenuRole::Cut:
        return @selector(cut:);
    case nk::NativeMenuRole::Copy:
        return @selector(copy:);
    case nk::NativeMenuRole::Paste:
        return @selector(paste:);
    case nk::NativeMenuRole::SelectAll:
        return @selector(selectAll:);
    case nk::NativeMenuRole::Minimize:
        return @selector(performMiniaturize:);
    case nk::NativeMenuRole::BringAllToFront:
        return @selector(arrangeInFront:);
    case nk::NativeMenuRole::Custom:
        return nil;
    }
    return nil;
}

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

struct NativeFrameClock {
    // UI-thread ownership: stop the link before its borrowed window/surface dies.
    CADisplayLink* link = nil;
    NKDisplayPollTarget* target = nil;
    NativeFrameClock() = default;
    NativeFrameClock(const NativeFrameClock&) = delete;
    NativeFrameClock& operator=(const NativeFrameClock&) = delete;
    NativeFrameClock(NativeFrameClock&&) = delete;
    NativeFrameClock& operator=(NativeFrameClock&&) = delete;

    ~NativeFrameClock() { stop(); }

    void stop() {
        if (@available(macOS 14.0, *)) {
            if (target) {
                // Manual polling resumes the renderer's ordinary presentation path.
                CALayer* layer = target->window.contentView.layer;
                if ([layer isKindOfClass:[CAMetalLayer class]]) {
                    static_cast<CAMetalLayer*>(layer).presentsWithTransaction = NO;
                }
                target->loop = nullptr;
                target->window = nil;
            }
            [link invalidate];
            [link release];
            [target release];
            link = nil;
            target = nil;
        }
    }
};

struct MacosBackend::Impl {
    NKAppDelegate* delegate = nil;
    CFRunLoopTimerRef poll_timer = nullptr;
    std::map<NativeSurface*, std::shared_ptr<NativeFrameClock>> frame_clocks;
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
    MacosSpellChecker spell_checker;

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
        const auto native_selector = native_menu_selector(item.native_role);
        [menu_item
            setEnabled:item.enabled && (native_selector != nil || !item.action_name.empty())];
        [menu_item setKeyEquivalentModifierMask:modifier_flags];
        if (!item.enabled) {
            [menu addItem:menu_item];
            continue;
        }

        if (native_selector != nil) {
            [menu_item setTarget:nil];
            [menu_item setAction:native_selector];
        } else if (!item.action_name.empty()) {
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
        impl_->frame_clocks.clear();
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
    loop.set_wake_handler([this] { wake_event_loop(); });
    impl_->quit_requested = false;
    impl_->exit_code = 0;

    // Timers and posted tasks remain independent of display cadence. Stalled
    // display clocks also use this pump so a hidden window cannot starve work.
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
            bool display_polling = false;
            std::vector<std::shared_ptr<NativeFrameClock>> stalled;
            if (@available(macOS 14.0, *)) {
                for (const auto& [surface, clock] : impl->frame_clocks) {
                    (void)surface;
                    const auto* target = clock->target;
                    if (target == nil) {
                        continue;
                    }
                    display_polling |= target->polling;
                    if (CACurrentMediaTime() - target->last_tick > 2 * target->interval) {
                        stalled.push_back(clock);
                    }
                }
            }
            if (impl->current_loop && !display_polling) {
                impl->current_loop->poll();
            }
            if (@available(macOS 14.0, *)) {
                for (const auto& clock : stalled) {
                    [clock->target flushFrames];
                    auto* target = clock->target;
                    if (target && target->frames.empty() &&
                        CACurrentMediaTime() - target->last_request > 0.1) {
                        clock->link.paused = YES;
                    }
                }
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
    if (@available(macOS 14.0, *)) {
        for (const auto& [surface, clock] : impl_->frame_clocks) {
            (void)surface;
            for (auto& frame : clock->target->frames) {
                (void)loop.post(std::move(frame), "window.request-frame");
            }
        }
    }
    impl_->frame_clocks.clear();
    if (impl_->poll_timer) {
        CFRunLoopTimerInvalidate(impl_->poll_timer);
        CFRelease(impl_->poll_timer);
        impl_->poll_timer = nullptr;
    }

    loop.set_wake_handler({});
    impl_->current_loop = nullptr;
    return impl_->exit_code;
}

bool MacosBackend::schedule_frame(NativeSurface& surface, std::function<void()> frame) {
    if (@available(macOS 14.0, *)) {
        if (impl_->current_loop == nullptr) {
            return false;
        }
        auto it = impl_->frame_clocks.find(&surface);
        if (it == impl_->frame_clocks.end()) {
            auto* window = static_cast<NSWindow*>(surface.native_handle());
            if (window == nil) {
                return false;
            }
            auto clock = std::make_shared<NativeFrameClock>();
            clock->target = [[NKDisplayPollTarget alloc] init];
            clock->target->loop = impl_->current_loop;
            clock->target->window = window;
            clock->target->interval = 1.0 / 120.0;
            clock->link = [[window displayLinkWithTarget:clock->target
                                                selector:@selector(displayTick:)] retain];
            if (clock->link == nil) {
                return false;
            }
            const float rate = static_cast<float>(window.screen.maximumFramesPerSecond);
            if (rate > 0) {
                clock->link.preferredFrameRateRange = CAFrameRateRangeMake(rate, rate, rate);
            }
            [clock->link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
            CALayer* layer = window.contentView.layer;
            if ([layer isKindOfClass:[CAMetalLayer class]]) {
                static_cast<CAMetalLayer*>(layer).presentsWithTransaction = YES;
            }
            it = impl_->frame_clocks.emplace(&surface, std::move(clock)).first;
        }
        it->second->target->frames.push_back(std::move(frame));
        it->second->target->last_request = CACurrentMediaTime();
        it->second->link.paused = NO;
        return true;
    }
    return false;
}

void MacosBackend::cancel_frames(NativeSurface& surface) {
    auto it = impl_->frame_clocks.find(&surface);
    if (it != impl_->frame_clocks.end()) {
        it->second->stop();
        impl_->frame_clocks.erase(it);
    }
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

SpellChecker* MacosBackend::spell_checker() {
    return &impl_->spell_checker;
}

bool MacosBackend::supports_open_file_dialog() const {
    return true;
}

void MacosBackend::show_open_file_dialog_async(std::string_view title,
                                               const std::vector<std::string>& filters,
                                               OpenFileDialogCallback callback) {
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

        [panel beginWithCompletionHandler:^(NSModalResponse response) {
          if (response == NSModalResponseOK && panel.URL) {
              if (callback) {
                  callback(std::string(panel.URL.path.UTF8String));
              }
          } else {
              if (callback) {
                  callback(Unexpected(FileDialogError::Cancelled));
              }
          }
        }];
    }
}

bool MacosBackend::supports_save_file_dialog() const {
    return true;
}

void MacosBackend::show_save_file_dialog_async(SaveFileDialogOptions options,
                                               SaveFileDialogCallback callback) {
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        [panel setTitle:[NSString stringWithUTF8String:options.title.c_str()]];
        [panel setCanCreateDirectories:YES];
        (void)options.confirm_overwrite; // NSSavePanel provides native overwrite confirmation.

        if (!options.suggested_filename.empty()) {
            [panel setNameFieldStringValue:[NSString stringWithUTF8String:options.suggested_filename
                                                                              .c_str()]];
        }

        if (!options.filters.empty()) {
            NSMutableArray<NSString*>* types = [NSMutableArray array];
            for (const auto& ext : options.filters) {
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

        [panel beginWithCompletionHandler:^(NSModalResponse response) {
          if (response == NSModalResponseOK && panel.URL) {
              if (callback) {
                  callback(std::string(panel.URL.path.UTF8String));
              }
          } else {
              if (callback) {
                  callback(Unexpected(FileDialogError::Cancelled));
              }
          }
        }];
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
