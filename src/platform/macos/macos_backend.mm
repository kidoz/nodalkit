/// @file macos_backend.mm
/// @brief macOS Cocoa platform backend implementation.

#include "macos_backend.h"

#include "macos_window.h"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <nk/runtime/event_loop.h>
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
