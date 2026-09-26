#include "../../accessibility/accessibility_tree.h"
#include "../../text/native_input_document.h"
#include "../../text/text_boundaries.h"
#include "text_input_routing.h"
/// @file macos_window.mm
/// @brief macOS Cocoa native surface implementation.

#include "macos_window.h"

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/platform/window_inspector.h>
#include <nk/ui_core/widget.h>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// macOS virtual key code → nk::KeyCode mapping
// ---------------------------------------------------------------------------

static nk::KeyCode macos_keycode_to_nk(unsigned short vk) {
    switch (vk) {
    // Letters
    case kVK_ANSI_A:
        return nk::KeyCode::A;
    case kVK_ANSI_B:
        return nk::KeyCode::B;
    case kVK_ANSI_C:
        return nk::KeyCode::C;
    case kVK_ANSI_D:
        return nk::KeyCode::D;
    case kVK_ANSI_E:
        return nk::KeyCode::E;
    case kVK_ANSI_F:
        return nk::KeyCode::F;
    case kVK_ANSI_G:
        return nk::KeyCode::G;
    case kVK_ANSI_H:
        return nk::KeyCode::H;
    case kVK_ANSI_I:
        return nk::KeyCode::I;
    case kVK_ANSI_J:
        return nk::KeyCode::J;
    case kVK_ANSI_K:
        return nk::KeyCode::K;
    case kVK_ANSI_L:
        return nk::KeyCode::L;
    case kVK_ANSI_M:
        return nk::KeyCode::M;
    case kVK_ANSI_N:
        return nk::KeyCode::N;
    case kVK_ANSI_O:
        return nk::KeyCode::O;
    case kVK_ANSI_P:
        return nk::KeyCode::P;
    case kVK_ANSI_Q:
        return nk::KeyCode::Q;
    case kVK_ANSI_R:
        return nk::KeyCode::R;
    case kVK_ANSI_S:
        return nk::KeyCode::S;
    case kVK_ANSI_T:
        return nk::KeyCode::T;
    case kVK_ANSI_U:
        return nk::KeyCode::U;
    case kVK_ANSI_V:
        return nk::KeyCode::V;
    case kVK_ANSI_W:
        return nk::KeyCode::W;
    case kVK_ANSI_X:
        return nk::KeyCode::X;
    case kVK_ANSI_Y:
        return nk::KeyCode::Y;
    case kVK_ANSI_Z:
        return nk::KeyCode::Z;

    // Numbers (top row)
    case kVK_ANSI_1:
        return nk::KeyCode::Num1;
    case kVK_ANSI_2:
        return nk::KeyCode::Num2;
    case kVK_ANSI_3:
        return nk::KeyCode::Num3;
    case kVK_ANSI_4:
        return nk::KeyCode::Num4;
    case kVK_ANSI_5:
        return nk::KeyCode::Num5;
    case kVK_ANSI_6:
        return nk::KeyCode::Num6;
    case kVK_ANSI_7:
        return nk::KeyCode::Num7;
    case kVK_ANSI_8:
        return nk::KeyCode::Num8;
    case kVK_ANSI_9:
        return nk::KeyCode::Num9;
    case kVK_ANSI_0:
        return nk::KeyCode::Num0;

    // Control keys
    case kVK_Return:
        return nk::KeyCode::Return;
    case kVK_Escape:
        return nk::KeyCode::Escape;
    case kVK_Delete:
        return nk::KeyCode::Backspace;
    case kVK_Tab:
        return nk::KeyCode::Tab;
    case kVK_Space:
        return nk::KeyCode::Space;

    // Punctuation
    case kVK_ANSI_Minus:
        return nk::KeyCode::Minus;
    case kVK_ANSI_Equal:
        return nk::KeyCode::Equals;
    case kVK_ANSI_LeftBracket:
        return nk::KeyCode::LeftBracket;
    case kVK_ANSI_RightBracket:
        return nk::KeyCode::RightBracket;
    case kVK_ANSI_Backslash:
        return nk::KeyCode::Backslash;
    case kVK_ANSI_Semicolon:
        return nk::KeyCode::Semicolon;
    case kVK_ANSI_Quote:
        return nk::KeyCode::Apostrophe;
    case kVK_ANSI_Grave:
        return nk::KeyCode::Grave;
    case kVK_ANSI_Comma:
        return nk::KeyCode::Comma;
    case kVK_ANSI_Period:
        return nk::KeyCode::Period;
    case kVK_ANSI_Slash:
        return nk::KeyCode::Slash;

    case kVK_CapsLock:
        return nk::KeyCode::CapsLock;

    // Function keys
    case kVK_F1:
        return nk::KeyCode::F1;
    case kVK_F2:
        return nk::KeyCode::F2;
    case kVK_F3:
        return nk::KeyCode::F3;
    case kVK_F4:
        return nk::KeyCode::F4;
    case kVK_F5:
        return nk::KeyCode::F5;
    case kVK_F6:
        return nk::KeyCode::F6;
    case kVK_F7:
        return nk::KeyCode::F7;
    case kVK_F8:
        return nk::KeyCode::F8;
    case kVK_F9:
        return nk::KeyCode::F9;
    case kVK_F10:
        return nk::KeyCode::F10;
    case kVK_F11:
        return nk::KeyCode::F11;
    case kVK_F12:
        return nk::KeyCode::F12;

    // Navigation
    case kVK_Home:
        return nk::KeyCode::Home;
    case kVK_PageUp:
        return nk::KeyCode::PageUp;
    case kVK_ForwardDelete:
        return nk::KeyCode::Delete;
    case kVK_End:
        return nk::KeyCode::End;
    case kVK_PageDown:
        return nk::KeyCode::PageDown;

    // Arrow keys
    case kVK_RightArrow:
        return nk::KeyCode::Right;
    case kVK_LeftArrow:
        return nk::KeyCode::Left;
    case kVK_DownArrow:
        return nk::KeyCode::Down;
    case kVK_UpArrow:
        return nk::KeyCode::Up;

    // Numpad
    case kVK_ANSI_KeypadDivide:
        return nk::KeyCode::NumpadDivide;
    case kVK_ANSI_KeypadMultiply:
        return nk::KeyCode::NumpadMultiply;
    case kVK_ANSI_KeypadMinus:
        return nk::KeyCode::NumpadMinus;
    case kVK_ANSI_KeypadPlus:
        return nk::KeyCode::NumpadPlus;
    case kVK_ANSI_KeypadEnter:
        return nk::KeyCode::NumpadEnter;
    case kVK_ANSI_Keypad1:
        return nk::KeyCode::Numpad1;
    case kVK_ANSI_Keypad2:
        return nk::KeyCode::Numpad2;
    case kVK_ANSI_Keypad3:
        return nk::KeyCode::Numpad3;
    case kVK_ANSI_Keypad4:
        return nk::KeyCode::Numpad4;
    case kVK_ANSI_Keypad5:
        return nk::KeyCode::Numpad5;
    case kVK_ANSI_Keypad6:
        return nk::KeyCode::Numpad6;
    case kVK_ANSI_Keypad7:
        return nk::KeyCode::Numpad7;
    case kVK_ANSI_Keypad8:
        return nk::KeyCode::Numpad8;
    case kVK_ANSI_Keypad9:
        return nk::KeyCode::Numpad9;
    case kVK_ANSI_Keypad0:
        return nk::KeyCode::Numpad0;
    case kVK_ANSI_KeypadDecimal:
        return nk::KeyCode::NumpadPeriod;
    case kVK_ANSI_KeypadClear:
        return nk::KeyCode::NumLock;

    // Modifiers
    case kVK_Control:
        return nk::KeyCode::LeftCtrl;
    case kVK_Shift:
        return nk::KeyCode::LeftShift;
    case kVK_Option:
        return nk::KeyCode::LeftAlt;
    case kVK_Command:
        return nk::KeyCode::LeftSuper;
    case kVK_RightControl:
        return nk::KeyCode::RightCtrl;
    case kVK_RightShift:
        return nk::KeyCode::RightShift;
    case kVK_RightOption:
        return nk::KeyCode::RightAlt;
    case kVK_RightCommand:
        return nk::KeyCode::RightSuper;

    default:
        return nk::KeyCode::Unknown;
    }
}

/// Convert NSEvent modifier flags to nk::Modifiers.
static nk::Modifiers macos_modifiers(NSEventModifierFlags flags) {
    nk::Modifiers mods = nk::Modifiers::None;
    if (flags & NSEventModifierFlagShift) {
        mods = mods | nk::Modifiers::Shift;
    }
    if (flags & NSEventModifierFlagControl) {
        mods = mods | nk::Modifiers::Ctrl;
    }
    if (flags & NSEventModifierFlagOption) {
        mods = mods | nk::Modifiers::Alt;
    }
    if (flags & NSEventModifierFlagCommand) {
        mods = mods | nk::Modifiers::Super;
    }
    return mods;
}

/// Convert NSEvent mouse button number to nk convention (1=left, 2=right, 3=middle).
static int macos_button_number(NSEvent* event) {
    switch (event.buttonNumber) {
    case 0:
        return 1; // left
    case 1:
        return 2; // right
    case 2:
        return 3; // middle
    default:
        return static_cast<int>(event.buttonNumber + 1);
    }
}

static nk::DragOperation drag_operation_from_mask(NSDragOperation mask) {
    if ((mask & NSDragOperationCopy) != 0) {
        return nk::DragOperation::Copy;
    }
    if ((mask & NSDragOperationMove) != 0) {
        return nk::DragOperation::Move;
    }
    if ((mask & NSDragOperationLink) != 0) {
        return nk::DragOperation::Link;
    }
    return nk::DragOperation::None;
}

static NSDragOperation ns_drag_operation(nk::DragOperation operation, NSDragOperation mask) {
    switch (operation) {
    case nk::DragOperation::Copy:
        return (mask & NSDragOperationCopy) != 0 ? NSDragOperationCopy : NSDragOperationNone;
    case nk::DragOperation::Move:
        return (mask & NSDragOperationMove) != 0 ? NSDragOperationMove : NSDragOperationNone;
    case nk::DragOperation::Link:
        return (mask & NSDragOperationLink) != 0 ? NSDragOperationLink : NSDragOperationNone;
    case nk::DragOperation::None:
    default:
        return NSDragOperationNone;
    }
}

static nk::Point drag_position_in_view(NSView* view, id<NSDraggingInfo> sender) {
    const NSPoint loc = [view convertPoint:[sender draggingLocation] fromView:nil];
    return {
        static_cast<float>(loc.x),
        static_cast<float>(loc.y),
    };
}

static std::shared_ptr<const nk::DragPayload> file_drop_payload(id<NSDraggingInfo> sender) {
    NSPasteboard* pasteboard = [sender draggingPasteboard];
    if (pasteboard == nil) {
        return nullptr;
    }

    NSDictionary* options = @{NSPasteboardURLReadingFileURLsOnlyKey : @YES};
    NSArray<NSURL*>* urls = [pasteboard readObjectsForClasses:@[ [NSURL class] ] options:options];
    if (urls == nil || urls.count == 0) {
        return nullptr;
    }

    std::vector<std::filesystem::path> files;
    files.reserve(static_cast<std::size_t>(urls.count));
    for (NSURL* url in urls) {
        if (url == nil || !url.isFileURL) {
            continue;
        }
        NSString* path = url.path;
        if (path == nil) {
            continue;
        }
        const char* utf8 = [path fileSystemRepresentation];
        if (utf8 != nullptr && utf8[0] != '\0') {
            files.emplace_back(utf8);
        }
    }

    if (files.empty()) {
        return nullptr;
    }
    return std::make_shared<nk::DragPayload>(nk::DragPayload::from_files(std::move(files)));
}

static nk::Modifiers current_drag_modifiers() {
    NSEvent* event = [NSApp currentEvent];
    return event != nil ? macos_modifiers(event.modifierFlags) : nk::Modifiers::None;
}

static NSString* ns_string(std::string_view value) {
    return [NSString stringWithUTF8String:std::string(value).c_str()];
}

// ---------------------------------------------------------------------------
// NKView — custom NSView for rendering and input
// ---------------------------------------------------------------------------

@class NKAccessibilityElement;

@interface NKView : NSView <NSTextInputClient, NSDraggingDestination>
@property(nonatomic, assign) nk::MacosSurface* surface;
- (void)synchronizeNativeState:(BOOL)moved;
- (nk::detail::AccessibilityTree*)accessibilityTree;
- (NKAccessibilityElement*)accessibilityElementForNode:(nk::detail::AccessibleId)node;
- (NSArray<id>*)accessibilityElementsForNodes:(const std::vector<nk::detail::AccessibleId>&)nodes;
- (NSRect)screenRectFromWindowRect:(nk::Rect)rect;
@end

// Native proxy for one exposed widget. It holds only an identity and reads every
// attribute from the view's AccessibilityTree when AppKit asks, so a proxy that
// AppKit keeps after its widget is gone answers as a defunct element instead of
// touching freed memory.
@interface NKAccessibilityElement : NSAccessibilityElement
- (instancetype)initWithNode:(nk::detail::AccessibleId)node view:(NKView*)view;
- (void)invalidate;
@end

static std::string objc_text_to_utf8(id value) {
    NSString* string = nil;
    if ([value isKindOfClass:[NSAttributedString class]]) {
        string = [(NSAttributedString*)value string];
    } else if ([value isKindOfClass:[NSString class]]) {
        string = (NSString*)value;
    }

    if (string == nil) {
        return {};
    }

    const char* utf8 = [string UTF8String];
    return utf8 != nullptr ? std::string(utf8) : std::string{};
}

static const nk::WidgetDebugNode* find_focused_debug_node(const nk::WidgetDebugNode& node) {
    if (node.focused) {
        return &node;
    }
    for (const auto& child : node.children) {
        if (const auto* focused = find_focused_debug_node(child); focused != nullptr) {
            return focused;
        }
    }
    return nullptr;
}

static NSRange ns_range(nk::detail::Utf16Range range) {
    return NSMakeRange(range.location, range.length);
}

static nk::detail::Utf16Range utf16_range(NSRange range) {
    return {range.location, range.length};
}

@implementation NKView {
    NSTrackingArea* tracking_area_;
    // Marked text the input context composes; the focused editor shows it as preedit.
    std::string marked_text_;
    std::size_t marked_selection_start_;
    std::size_t marked_selection_end_;
    std::optional<nk::Rect> input_caret_rect_;
    // The view retains one proxy per identity until its widget is destroyed.
    std::unique_ptr<nk::detail::AccessibilityTree> accessibility_tree_;
    std::unordered_map<nk::detail::AccessibleId, NKAccessibilityElement*> accessibility_elements_;
    std::optional<nk::detail::AccessibleId> accessibility_focus_;
    std::string accessibility_focus_value_;
    BOOL accessibility_active_;
}

- (instancetype)initWithFrame:(NSRect)frame surface:(nk::MacosSurface*)surface {
    self = [super initWithFrame:frame];
    if (self) {
        _surface = surface;
        [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
        [self updateTrackingAreas];
    }
    return self;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    (void)event;
    return YES;
}

- (BOOL)isFlipped {
    // Use top-left origin to match nk coordinate system.
    return YES;
}

- (void)dealloc {
    [self releaseAccessibilityElements];
    [super dealloc];
}

- (void)setSurface:(nk::MacosSurface*)surface {
    if (surface != _surface) {
        // The tree and every proxy refer to the owning Window through the surface.
        [self releaseAccessibilityElements];
        _surface = surface;
    }
}

// -- Accessibility --

// The view is the window's accessibility root; widget proxies hang below it.
- (BOOL)isAccessibilityElement {
    return YES;
}

- (NSString*)accessibilityRole {
    return NSAccessibilityGroupRole;
}

- (NSString*)accessibilityLabel {
    if (!_surface) {
        return @"NodalKit window";
    }
    return ns_string(_surface->owner().title());
}

- (NSString*)accessibilityTitle {
    return self.accessibilityLabel;
}

- (NSArray<id>*)accessibilityChildren {
    auto* tree = [self accessibilityTree];
    if (tree == nullptr) {
        return @[];
    }
    [self purgeAccessibilityElements];
    return [self accessibilityElementsForNodes:tree->root_children()];
}

- (id)accessibilityFocusedUIElement {
    if (auto* tree = [self accessibilityTree]; tree != nullptr) {
        if (const auto focused = tree->focused(); focused.has_value()) {
            return [self accessibilityElementForNode:*focused];
        }
    }
    return self;
}

- (id)accessibilityHitTest:(NSPoint)point {
    auto* tree = [self accessibilityTree];
    if (tree == nullptr || self.window == nil) {
        return self;
    }
    const NSPoint local = [self convertPoint:[self.window convertPointFromScreen:point]
                                    fromView:nil];
    if (const auto hit =
            tree->hit_test({.x = static_cast<float>(local.x), .y = static_cast<float>(local.y)});
        hit.has_value()) {
        return [self accessibilityElementForNode:*hit];
    }
    return self;
}

// AppKit services accessibility requests on the main thread. Any other thread
// gets no tree rather than a racing walk of the widget hierarchy.
- (nk::detail::AccessibilityTree*)accessibilityTree {
    if (!_surface || ![NSThread isMainThread]) {
        return nullptr;
    }
    if (!accessibility_tree_) {
        accessibility_tree_ = std::make_unique<nk::detail::AccessibilityTree>(_surface->owner());
    }
    accessibility_active_ = YES;
    return accessibility_tree_.get();
}

- (NKAccessibilityElement*)accessibilityElementForNode:(nk::detail::AccessibleId)node {
    if (const auto found = accessibility_elements_.find(node);
        found != accessibility_elements_.end()) {
        return found->second;
    }
    auto* element = [[NKAccessibilityElement alloc] initWithNode:node view:self];
    accessibility_elements_.emplace(node, element);
    return element;
}

- (NSArray<id>*)accessibilityElementsForNodes:(const std::vector<nk::detail::AccessibleId>&)nodes {
    NSMutableArray<id>* elements = [NSMutableArray arrayWithCapacity:nodes.size()];
    for (const auto node : nodes) {
        [elements addObject:[self accessibilityElementForNode:node]];
    }
    return elements;
}

- (NSRect)screenRectFromWindowRect:(nk::Rect)rect {
    if (self.window == nil) {
        return NSZeroRect;
    }
    const NSRect local = NSMakeRect(rect.x, rect.y, rect.width, rect.height);
    return [self.window convertRectToScreen:[self convertRect:local toView:nil]];
}

- (void)retireAccessibilityElement:(NKAccessibilityElement*)element {
    NSAccessibilityPostNotification(element, NSAccessibilityUIElementDestroyedNotification);
    [element invalidate];
    // AppKit may still be delivering the notification; drop our ownership later.
    [element autorelease];
}

- (void)purgeAccessibilityElements {
    if (!accessibility_tree_) {
        return;
    }
    for (const auto node : accessibility_tree_->purge()) {
        if (const auto found = accessibility_elements_.find(node);
            found != accessibility_elements_.end()) {
            [self retireAccessibilityElement:found->second];
            accessibility_elements_.erase(found);
        }
    }
}

- (void)releaseAccessibilityElements {
    for (const auto& [node, element] : accessibility_elements_) {
        [self retireAccessibilityElement:element];
    }
    accessibility_elements_.clear();
    accessibility_tree_.reset();
    accessibility_focus_.reset();
    accessibility_focus_value_.clear();
    accessibility_active_ = NO;
}

// Announce focus and value changes caused by toolkit events. Nothing is posted
// until an assistive technology has asked for the tree.
- (void)synchronizeAccessibility {
    if (!accessibility_active_) {
        return;
    }
    auto* tree = [self accessibilityTree];
    if (tree == nullptr) {
        return;
    }
    [self purgeAccessibilityElements];
    const auto focused = tree->focused();
    std::string value;
    if (focused.has_value()) {
        if (const auto info = tree->info(*focused); info.has_value()) {
            value = info->value;
        }
    }
    if (focused != accessibility_focus_) {
        accessibility_focus_ = focused;
        accessibility_focus_value_ = std::move(value);
        id target = focused.has_value() ? [self accessibilityElementForNode:*focused] : self;
        NSAccessibilityPostNotification(target, NSAccessibilityFocusedUIElementChangedNotification);
    } else if (focused.has_value() && value != accessibility_focus_value_) {
        accessibility_focus_value_ = std::move(value);
        NSAccessibilityPostNotification([self accessibilityElementForNode:*focused],
                                        NSAccessibilityValueChangedNotification);
    }
}

- (void)synchronizeNativeState:(BOOL)moved {
    [self synchronizeInputContext:moved];
    [self synchronizeAccessibility];
}

// -- Tracking areas --

- (void)updateTrackingAreas {
    if (tracking_area_) {
        [self removeTrackingArea:tracking_area_];
        tracking_area_ = nil;
    }

    NSTrackingAreaOptions opts = NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                                 NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    tracking_area_ = [[NSTrackingArea alloc] initWithRect:self.bounds
                                                  options:opts
                                                    owner:self
                                                 userInfo:nil];
    [self addTrackingArea:tracking_area_];
    [super updateTrackingAreas];
}

// -- Drawing --

- (void)drawRect:(NSRect)dirtyRect {
    if (!_surface || !_surface->has_pixels()) {
        return;
    }

    int w = _surface->pixel_width();
    int h = _surface->pixel_height();
    const uint8_t* data = _surface->pixel_data();

    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmap = CGBitmapContextCreate(
        const_cast<uint8_t*>(data),
        static_cast<size_t>(w),
        static_cast<size_t>(h),
        8,                          // bits per component
        static_cast<size_t>(w) * 4, // bytes per row
        cs,
        kCGImageAlphaPremultipliedLast | static_cast<CGBitmapInfo>(kCGBitmapByteOrderDefault));
    CGColorSpaceRelease(cs);

    if (!bitmap) {
        return;
    }

    CGImageRef image = CGBitmapContextCreateImage(bitmap);
    CGContextRelease(bitmap);

    if (!image) {
        return;
    }

    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    if (ctx) {
        NSRect bounds = self.bounds;
        // isFlipped == YES, but CGContext draws bottom-up; compensate.
        CGContextSaveGState(ctx);
        CGContextTranslateCTM(ctx, 0, bounds.size.height);
        CGContextScaleCTM(ctx, 1.0, -1.0);
        CGContextDrawImage(ctx, CGRectMake(0, 0, bounds.size.width, bounds.size.height), image);
        CGContextRestoreGState(ctx);
    }

    CGImageRelease(image);
}

// -- Mouse events --

- (void)mouseDown:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    [self commitMarkedText];
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Press;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.button = 1;
    me.click_count = static_cast<int>(event.clickCount);
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
    [self synchronizeNativeState:NO];
}

- (void)mouseUp:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Release;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.button = 1;
    me.click_count = static_cast<int>(event.clickCount);
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
    [self synchronizeNativeState:NO];
}

- (void)rightMouseDown:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    [self commitMarkedText];
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Press;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.button = 2;
    me.click_count = static_cast<int>(event.clickCount);
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
    [self synchronizeNativeState:NO];
}

- (void)rightMouseUp:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Release;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.button = 2;
    me.click_count = static_cast<int>(event.clickCount);
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
    [self synchronizeNativeState:NO];
}

- (void)otherMouseDown:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    [self commitMarkedText];
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Press;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.button = macos_button_number(event);
    me.click_count = static_cast<int>(event.clickCount);
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
    [self synchronizeNativeState:NO];
}

- (void)otherMouseUp:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Release;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.button = macos_button_number(event);
    me.click_count = static_cast<int>(event.clickCount);
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
    [self synchronizeNativeState:NO];
}

- (void)mouseMoved:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Move;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
}

- (void)mouseDragged:(NSEvent*)event {
    // Treat drags as moves so the widget system sees continuous tracking.
    [self mouseMoved:event];
}

- (void)rightMouseDragged:(NSEvent*)event {
    [self mouseMoved:event];
}

- (void)otherMouseDragged:(NSEvent*)event {
    [self mouseMoved:event];
}

- (void)mouseEntered:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Enter;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
}

- (void)mouseExited:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Leave;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
}

- (void)scrollWheel:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Scroll;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.scroll_dx = static_cast<float>(event.scrollingDeltaX);
    me.scroll_dy = static_cast<float>(event.scrollingDeltaY);
    me.precise_scrolling = event.hasPreciseScrollingDeltas;
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
    [self synchronizeNativeState:NO];
}

// -- Drag destination events --

- (NSDragOperation)dispatchDraggingInfo:(id<NSDraggingInfo>)sender
                                   type:(nk::DragDropEventType)type {
    if (!_surface) {
        return NSDragOperationNone;
    }

    auto payload = file_drop_payload(sender);
    if (payload == nullptr) {
        return NSDragOperationNone;
    }

    const auto source_mask = [sender draggingSourceOperationMask];
    const auto requested_operation = drag_operation_from_mask(source_mask);
    if (requested_operation == nk::DragOperation::None) {
        return NSDragOperationNone;
    }

    const auto accepted = _surface->owner().dispatch_drag_drop_event({
        .type = type,
        .position = drag_position_in_view(self, sender),
        .payload = std::move(payload),
        .requested_operation = requested_operation,
        .modifiers = current_drag_modifiers(),
        .external = true,
    });
    return ns_drag_operation(accepted, source_mask);
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return [self dispatchDraggingInfo:sender type:nk::DragDropEventType::Enter];
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    return [self dispatchDraggingInfo:sender type:nk::DragDropEventType::Motion];
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
    if (!_surface) {
        return;
    }

    auto payload = file_drop_payload(sender);
    if (payload == nullptr) {
        return;
    }

    (void)_surface->owner().dispatch_drag_drop_event({
        .type = nk::DragDropEventType::Leave,
        .position = drag_position_in_view(self, sender),
        .payload = std::move(payload),
        .requested_operation = drag_operation_from_mask([sender draggingSourceOperationMask]),
        .modifiers = current_drag_modifiers(),
        .external = true,
    });
}

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
    return file_drop_payload(sender) != nullptr;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    return [self dispatchDraggingInfo:sender
                                 type:nk::DragDropEventType::Drop] != NSDragOperationNone;
}

// -- Keyboard events --

- (void)keyDown:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    [self synchronizeNativeState:NO];
    const auto modifiers = macos_modifiers(event.modifierFlags);
    const auto key = macos_keycode_to_nk(event.keyCode);
    const bool text_input_active = _surface->owner().current_text_input_state().has_value();
    if (text_input_active &&
        !nk::detail::macos_dispatch_key_directly(key, modifiers, [self hasMarkedText])) {
        [self interpretKeyEvents:@[ event ]];
    } else {
        nk::KeyEvent ke{};
        ke.type = nk::KeyEvent::Type::Press;
        ke.key = key;
        ke.modifiers = modifiers;
        ke.is_repeat = event.isARepeat;
        _surface->owner().dispatch_key_event(ke);
    }
    [self synchronizeNativeState:NO];
}

- (void)keyUp:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    nk::KeyEvent ke{};
    ke.type = nk::KeyEvent::Type::Release;
    ke.key = macos_keycode_to_nk(event.keyCode);
    ke.modifiers = macos_modifiers(event.modifierFlags);
    ke.is_repeat = false;
    _surface->owner().dispatch_key_event(ke);
    [self synchronizeNativeState:NO];
}

// -- Text input client --

// Cocoa addresses the committed text with the marked text in place of the
// selection it replaces; nullopt when no focused editor accepts text input.
- (std::optional<nk::detail::NativeInputDocument>)inputDocument {
    if (!_surface) {
        return std::nullopt;
    }
    const auto state = _surface->owner().current_text_input_state();
    if (!state.has_value()) {
        return std::nullopt;
    }
    if (!state->composing || marked_text_.empty()) {
        return nk::detail::NativeInputDocument(state->text, state->cursor, state->anchor);
    }
    return nk::detail::NativeInputDocument(state->text,
                                           state->cursor,
                                           state->anchor,
                                           marked_text_,
                                           marked_selection_start_,
                                           marked_selection_end_);
}

- (void)clearMarkedTextState {
    marked_text_.clear();
    marked_selection_start_ = 0;
    marked_selection_end_ = 0;
}

// The toolkit ends compositions on its own (focus changes, read-only
// transitions, Escape handled by an editor). Tell the input context so it does
// not keep composing text that the editor no longer shows, and let candidate
// windows follow the caret after scrolling or window movement.
- (void)synchronizeInputContext:(BOOL)moved {
    if (!_surface) {
        return;
    }
    const auto state = _surface->owner().current_text_input_state();
    if (!marked_text_.empty() && !(state.has_value() && state->composing)) {
        [self clearMarkedTextState];
        [[self inputContext] discardMarkedText];
    }
    std::optional<nk::Rect> caret_rect;
    if (state.has_value()) {
        caret_rect = state->caret_rect;
    }
    if (moved || caret_rect != input_caret_rect_) {
        input_caret_rect_ = caret_rect;
        [[self inputContext] invalidateCharacterCoordinates];
    }
}

// Cocoa text views keep marked text as typed when a click moves the caret.
- (void)commitMarkedText {
    if (![self hasMarkedText]) {
        return;
    }
    nk::TextInputEvent te{};
    te.type = nk::TextInputEvent::Type::Commit;
    te.text = marked_text_;
    [self clearMarkedTextState];
    [[self inputContext] discardMarkedText];
    _surface->owner().dispatch_text_input_event(te);
}

- (BOOL)hasMarkedText {
    if (marked_text_.empty() || !_surface) {
        return NO;
    }
    const auto state = _surface->owner().current_text_input_state();
    return state.has_value() && state->composing;
}

- (NSRange)markedRange {
    const auto document = [self inputDocument];
    if (document.has_value()) {
        if (const auto range = document->marked_range(); range.has_value()) {
            return ns_range(*range);
        }
    }
    return NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange {
    const auto document = [self inputDocument];
    return document.has_value() ? ns_range(document->selected_range()) : NSMakeRange(NSNotFound, 0);
}

- (void)setMarkedText:(id)string
        selectedRange:(NSRange)selectedRange
     replacementRange:(NSRange)replacementRange {
    const auto document = [self inputDocument];
    if (!document.has_value()) {
        [self clearMarkedTextState];
        return;
    }
    auto text = objc_text_to_utf8(string);
    const auto length = nk::detail::utf16_offset_from_utf8(text, text.size());
    const auto start = std::min<std::size_t>(selectedRange.location, length);
    const auto end = start + std::min<std::size_t>(selectedRange.length, length - start);
    nk::TextInputEvent te{};
    te.type = nk::TextInputEvent::Type::Preedit;
    te.selection_start = nk::detail::utf8_offset_from_utf16(text, start);
    te.selection_end = nk::detail::utf8_offset_from_utf16(text, end, true);
    if (!text.empty() && replacementRange.location != NSNotFound) {
        te.replacement_range = document->committed_replacement(utf16_range(replacementRange));
    }
    marked_text_ = text;
    marked_selection_start_ = te.selection_start;
    marked_selection_end_ = te.selection_end;
    te.text = std::move(text);
    _surface->owner().dispatch_text_input_event(te);
}

// NSTextInputClient contract: accept the marked text as if it had been inserted.
- (void)unmarkText {
    if (!_surface) {
        return;
    }
    if (![self hasMarkedText]) {
        [self clearMarkedTextState];
        return;
    }
    nk::TextInputEvent te{};
    te.type = nk::TextInputEvent::Type::Commit;
    te.text = marked_text_;
    [self clearMarkedTextState];
    _surface->owner().dispatch_text_input_event(te);
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
    return @[];
}

- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                               actualRange:(NSRangePointer)actualRange {
    std::optional<nk::detail::NativeInputSubstring> substring;
    if (range.location != NSNotFound) {
        if (const auto document = [self inputDocument]; document.has_value()) {
            substring = document->substring(utf16_range(range));
        }
    }
    if (actualRange != nullptr) {
        *actualRange =
            substring.has_value() ? ns_range(substring->range) : NSMakeRange(NSNotFound, 0);
    }
    if (!substring.has_value()) {
        return nil;
    }
    return [[[NSAttributedString alloc] initWithString:ns_string(substring->text)] autorelease];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    if (!_surface) {
        return;
    }
    nk::TextInputEvent te{};
    te.type = nk::TextInputEvent::Type::Commit;
    te.text = objc_text_to_utf8(string);
    if (replacementRange.location != NSNotFound) {
        if (const auto document = [self inputDocument]; document.has_value()) {
            te.replacement_range = document->committed_replacement(utf16_range(replacementRange));
        }
    }
    [self clearMarkedTextState];
    _surface->owner().dispatch_text_input_event(te);
}

// Pointer hit testing into editor text is not exposed to backends yet.
- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    (void)point;
    return NSNotFound;
}

// Editors expose caret geometry only, so every range maps to the composed caret.
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    if (actualRange != nullptr) {
        const auto document = [self inputDocument];
        *actualRange = document.has_value() && range.location != NSNotFound
                           ? ns_range(document->clamp(utf16_range(range)))
                           : NSMakeRange(NSNotFound, 0);
    }

    NSRect local_rect = NSMakeRect(0.0, 0.0, 1.0, 20.0);
    if (_surface) {
        if (const auto caret_rect = _surface->owner().current_text_input_caret_rect();
            caret_rect.has_value()) {
            local_rect = NSMakeRect(caret_rect->x,
                                    caret_rect->y,
                                    std::max(1.0F, caret_rect->width),
                                    std::max(20.0F, caret_rect->height));
        } else {
            const auto tree = _surface->owner().inspector().debug_tree();
            if (const auto* focused = find_focused_debug_node(tree); focused != nullptr) {
                local_rect = NSMakeRect(focused->allocation.x,
                                        focused->allocation.y,
                                        std::max(1.0F, focused->allocation.width),
                                        std::max(20.0F, focused->allocation.height));
            }
        }
    }

    const NSRect window_rect = [self convertRect:local_rect toView:nil];
    return [self.window convertRectToScreen:window_rect];
}

- (void)flagsChanged:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    nk::KeyCode key = macos_keycode_to_nk(event.keyCode);
    if (key == nk::KeyCode::Unknown) {
        return;
    }

    // Determine press vs release by checking if the modifier flag is set.
    bool pressed = false;
    switch (event.keyCode) {
    case kVK_Shift:
    case kVK_RightShift:
        pressed = (event.modifierFlags & NSEventModifierFlagShift) != 0;
        break;
    case kVK_Control:
    case kVK_RightControl:
        pressed = (event.modifierFlags & NSEventModifierFlagControl) != 0;
        break;
    case kVK_Option:
    case kVK_RightOption:
        pressed = (event.modifierFlags & NSEventModifierFlagOption) != 0;
        break;
    case kVK_Command:
    case kVK_RightCommand:
        pressed = (event.modifierFlags & NSEventModifierFlagCommand) != 0;
        break;
    case kVK_CapsLock:
        pressed = (event.modifierFlags & NSEventModifierFlagCapsLock) != 0;
        break;
    default:
        return;
    }

    nk::KeyEvent ke{};
    ke.type = pressed ? nk::KeyEvent::Type::Press : nk::KeyEvent::Type::Release;
    ke.key = key;
    ke.modifiers = macos_modifiers(event.modifierFlags);
    ke.is_repeat = false;
    _surface->owner().dispatch_key_event(ke);
}

@end

// ---------------------------------------------------------------------------
// NKAccessibilityElement — native proxy for an exposed widget
// ---------------------------------------------------------------------------

static bool accessibility_type_is(const nk::detail::AccessibleNodeInfo& info,
                                  std::string_view type) {
    return info.type_name == type || info.type_name.ends_with("::" + std::string(type));
}

static bool accessibility_has_state(const nk::detail::AccessibleNodeInfo& info,
                                    nk::StateFlags flag) {
    return (info.state & flag) == flag;
}

static NSString* macos_accessibility_role(const nk::detail::AccessibleNodeInfo& info) {
    using Role = nk::AccessibleRole;
    if (accessibility_type_is(info, "ComboBox")) {
        return NSAccessibilityPopUpButtonRole;
    }
    if (accessibility_type_is(info, "Expander")) {
        return NSAccessibilityDisclosureTriangleRole;
    }
    if (accessibility_type_is(info, "TextArea")) {
        return NSAccessibilityTextAreaRole;
    }
    switch (info.role) {
    case Role::Button:
        return NSAccessibilityButtonRole;
    case Role::CheckBox:
    case Role::ToggleButton:
        return NSAccessibilityCheckBoxRole;
    case Role::Grid:
        return NSAccessibilityTableRole;
    case Role::GridCell:
        return NSAccessibilityCellRole;
    case Role::Image:
        return NSAccessibilityImageRole;
    case Role::Label:
        return NSAccessibilityStaticTextRole;
    case Role::Link:
        return NSAccessibilityLinkRole;
    case Role::List:
        return NSAccessibilityListRole;
    case Role::ListItem:
    case Role::TreeItem:
        return NSAccessibilityRowRole;
    case Role::Menu:
        return NSAccessibilityMenuRole;
    case Role::MenuBar:
        return NSAccessibilityMenuBarRole;
    case Role::MenuItem:
        return NSAccessibilityMenuItemRole;
    case Role::ProgressBar:
        return NSAccessibilityProgressIndicatorRole;
    case Role::RadioButton:
    case Role::Tab:
        return NSAccessibilityRadioButtonRole;
    case Role::ScrollBar:
        return NSAccessibilityScrollBarRole;
    case Role::Slider:
    case Role::SpinButton:
        return NSAccessibilitySliderRole;
    case Role::TabList:
        return NSAccessibilityTabGroupRole;
    case Role::TextInput:
        return NSAccessibilityTextFieldRole;
    case Role::Toolbar:
        return NSAccessibilityToolbarRole;
    case Role::Tree:
        return NSAccessibilityOutlineRole;
    case Role::None:
    case Role::Dialog:
    case Role::Group:
    case Role::Separator:
    case Role::Status:
    case Role::TabPanel:
    case Role::Window:
        return NSAccessibilityGroupRole;
    }
    return NSAccessibilityGroupRole;
}

static NSString* macos_accessibility_subrole(const nk::detail::AccessibleNodeInfo& info) {
    if (accessibility_type_is(info, "Switch")) {
        return NSAccessibilitySwitchSubrole;
    }
    switch (info.role) {
    case nk::AccessibleRole::ToggleButton:
        return accessibility_type_is(info, "Expander") ? nil : NSAccessibilityToggleSubrole;
    case nk::AccessibleRole::Tab:
        return NSAccessibilityTabButtonSubrole;
    case nk::AccessibleRole::TreeItem:
        return NSAccessibilityOutlineRowSubrole;
    default:
        return nil;
    }
}

static std::optional<double> accessibility_number(const std::string& text) {
    if (text.empty()) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

@implementation NKAccessibilityElement {
    nk::detail::AccessibleId node_;
    NKView* view_; // Not retained: the view invalidates its proxies before it goes away.
}

- (instancetype)initWithNode:(nk::detail::AccessibleId)node view:(NKView*)view {
    self = [super init];
    if (self) {
        node_ = node;
        view_ = view;
    }
    return self;
}

- (void)invalidate {
    view_ = nil;
}

- (nk::detail::AccessibilityTree*)tree {
    return view_ != nil ? [view_ accessibilityTree] : nullptr;
}

- (std::optional<nk::detail::AccessibleNodeInfo>)info {
    auto* tree = [self tree];
    return tree != nullptr ? tree->info(node_) : std::nullopt;
}

- (BOOL)isAccessibilityElement {
    const auto info = [self info];
    return info.has_value() && info->role != nk::AccessibleRole::Separator;
}

- (id)accessibilityParent {
    auto* tree = [self tree];
    if (tree == nullptr || !tree->is_exposed(node_)) {
        return nil;
    }
    if (const auto parent = tree->parent(node_); parent.has_value()) {
        return [view_ accessibilityElementForNode:*parent];
    }
    return view_;
}

- (NSArray<id>*)accessibilityChildren {
    auto* tree = [self tree];
    return tree != nullptr ? [view_ accessibilityElementsForNodes:tree->children(node_)] : @[];
}

- (id)accessibilityWindow {
    return view_.window;
}

- (id)accessibilityTopLevelUIElement {
    return view_.window;
}

- (NSString*)accessibilityRole {
    const auto info = [self info];
    return info.has_value() ? macos_accessibility_role(*info) : NSAccessibilityUnknownRole;
}

- (NSString*)accessibilitySubrole {
    const auto info = [self info];
    return info.has_value() ? macos_accessibility_subrole(*info) : nil;
}

- (NSString*)accessibilityRoleDescription {
    return NSAccessibilityRoleDescription(self.accessibilityRole, self.accessibilitySubrole);
}

- (NSString*)accessibilityLabel {
    const auto info = [self info];
    // Static text carries its text as the value, like AppKit labels.
    if (!info.has_value() || info->name.empty() || info->role == nk::AccessibleRole::Label) {
        return nil;
    }
    return ns_string(info->name);
}

- (id)accessibilityValue {
    const auto info = [self info];
    if (!info.has_value()) {
        return nil;
    }
    using Role = nk::AccessibleRole;
    switch (info->role) {
    case Role::CheckBox:
    case Role::RadioButton:
    case Role::ToggleButton:
        return @(accessibility_has_state(*info, nk::StateFlags::Checked) ? 1 : 0);
    case Role::Tab:
        return @(accessibility_has_state(*info, nk::StateFlags::Selected) ||
                         accessibility_has_state(*info, nk::StateFlags::Checked)
                     ? 1
                     : 0);
    case Role::Label:
        return ns_string(info->name);
    case Role::TextInput:
        return ns_string(info->value);
    case Role::ProgressBar:
    case Role::Slider:
    case Role::SpinButton:
        if (const auto number = accessibility_number(info->value); number.has_value()) {
            return @(*number);
        }
        break;
    default:
        break;
    }
    return info->value.empty() ? nil : ns_string(info->value);
}

- (NSString*)accessibilityHelp {
    const auto info = [self info];
    return info.has_value() && !info->description.empty() ? ns_string(info->description) : nil;
}

// Debug names identify elements for UI automation; they are never spoken.
- (NSString*)accessibilityIdentifier {
    const auto info = [self info];
    return info.has_value() && !info->debug_name.empty() ? ns_string(info->debug_name) : nil;
}

- (BOOL)isAccessibilityEnabled {
    const auto info = [self info];
    return info.has_value() && info->enabled;
}

- (BOOL)isAccessibilityFocused {
    const auto info = [self info];
    return info.has_value() && info->focused;
}

- (void)setAccessibilityFocused:(BOOL)focused {
    auto* tree = [self tree];
    if (focused && tree != nullptr && tree->focus(node_)) {
        [view_ synchronizeNativeState:NO];
    }
}

- (BOOL)isAccessibilitySelected {
    const auto info = [self info];
    return info.has_value() && accessibility_has_state(*info, nk::StateFlags::Selected);
}

- (NSRect)accessibilityFrame {
    const auto info = [self info];
    return info.has_value() ? [view_ screenRectFromWindowRect:info->bounds] : NSZeroRect;
}

- (id)accessibilityHitTest:(NSPoint)point {
    return view_ != nil ? [view_ accessibilityHitTest:point] : nil;
}

- (id)accessibilityFocusedUIElement {
    return view_ != nil ? [view_ accessibilityFocusedUIElement] : nil;
}

- (BOOL)accessibilityPerformPress {
    auto* tree = [self tree];
    if (tree == nullptr) {
        return NO;
    }
    const bool performed = tree->perform(node_, nk::AccessibleAction::Activate) ||
                           tree->perform(node_, nk::AccessibleAction::Toggle);
    // The action may have closed this window, which invalidates the proxy.
    [view_ synchronizeNativeState:NO];
    return performed ? YES : NO;
}

- (BOOL)isAccessibilitySelectorAllowed:(SEL)selector {
    if (selector == @selector(accessibilityPerformPress)) {
        const auto info = [self info];
        return info.has_value() && info->enabled &&
               std::ranges::any_of(info->actions, [](nk::AccessibleAction action) {
                   return action == nk::AccessibleAction::Activate ||
                          action == nk::AccessibleAction::Toggle;
               });
    }
    if (selector == @selector(setAccessibilityFocused:)) {
        const auto info = [self info];
        return info.has_value() && info->enabled && info->focusable;
    }
    return [super isAccessibilitySelectorAllowed:selector];
}

// -- Text --

- (NSInteger)accessibilityNumberOfCharacters {
    const auto info = [self info];
    return info.has_value() ? static_cast<NSInteger>(nk::detail::utf16_offset_from_utf8(
                                  info->value, info->value.size()))
                            : 0;
}

- (NSRange)accessibilityVisibleCharacterRange {
    return NSMakeRange(0, static_cast<NSUInteger>(self.accessibilityNumberOfCharacters));
}

- (NSRange)accessibilitySelectedTextRange {
    const auto info = [self info];
    if (!info.has_value() || info->role != nk::AccessibleRole::TextInput) {
        return NSMakeRange(0, 0);
    }
    // Secure editors publish no committed text, so their state never matches the masked value.
    if (info->focused && view_.surface != nullptr) {
        if (const auto state = view_.surface->owner().current_text_input_state();
            state.has_value() && state->text == info->value) {
            return ns_range(
                nk::detail::NativeInputDocument(state->text, state->cursor, state->anchor)
                    .selected_range());
        }
    }
    return NSMakeRange(static_cast<NSUInteger>(self.accessibilityNumberOfCharacters), 0);
}

- (NSString*)accessibilitySelectedText {
    const auto info = [self info];
    if (!info.has_value()) {
        return nil;
    }
    const auto range = self.accessibilitySelectedTextRange;
    const auto substring =
        nk::detail::NativeInputDocument(info->value, 0, 0).substring(utf16_range(range));
    return substring.has_value() ? ns_string(substring->text) : nil;
}

@end

// ---------------------------------------------------------------------------
// NKWindowDelegate — handles window-level events
// ---------------------------------------------------------------------------

@interface NKWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) nk::MacosSurface* surface;
@end

@implementation NKWindowDelegate

- (BOOL)windowShouldClose:(id)sender {
    if (_surface) {
        nk::WindowEvent we{};
        we.type = nk::WindowEvent::Type::Close;
        _surface->owner().dispatch_window_event(we);
    }
    // Let the nk layer decide whether to actually close.
    return NO;
}

- (void)windowDidResize:(NSNotification*)notification {
    if (!_surface) {
        return;
    }
    nk::Size sz = _surface->size();
    nk::WindowEvent we{};
    we.type = nk::WindowEvent::Type::Resize;
    we.width = static_cast<int>(sz.width);
    we.height = static_cast<int>(sz.height);
    _surface->owner().dispatch_window_event(we);
    _surface->sync_native_state(true);
}

- (void)windowDidMove:(NSNotification*)notification {
    if (_surface) {
        _surface->sync_native_state(true);
    }
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
    if (!_surface) {
        return;
    }
    nk::WindowEvent we{};
    we.type = nk::WindowEvent::Type::FocusIn;
    _surface->owner().dispatch_window_event(we);
    _surface->sync_native_state(false);
}

- (void)windowDidResignKey:(NSNotification*)notification {
    if (!_surface) {
        return;
    }
    nk::WindowEvent we{};
    we.type = nk::WindowEvent::Type::FocusOut;
    _surface->owner().dispatch_window_event(we);
    _surface->sync_native_state(false);
}

- (void)windowDidExpose:(NSNotification*)notification {
    if (!_surface) {
        return;
    }
    nk::WindowEvent we{};
    we.type = nk::WindowEvent::Type::Expose;
    _surface->owner().dispatch_window_event(we);
}

- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
    if (!_surface) {
        return;
    }
    nk::WindowEvent we{};
    we.type = nk::WindowEvent::Type::Expose;
    _surface->owner().dispatch_window_event(we);
    _surface->sync_native_state(true);
}

@end

// ---------------------------------------------------------------------------
// Native toolbar delegate
// ---------------------------------------------------------------------------

@interface NKToolbarDelegate : NSObject <NSToolbarDelegate, NSSearchFieldDelegate>
@property(nonatomic, assign) const nk::NativeToolbarConfig* config;
- (void)toolbarAction:(NSToolbarItem*)sender;
@end

@implementation NKToolbarDelegate

- (const nk::NativeToolbarItem*)itemWithIdentifier:(NSString*)identifier {
    if (!self.config) {
        return nullptr;
    }
    const char* key_utf8 = [identifier UTF8String];
    std::string key = key_utf8 != nullptr ? key_utf8 : "";
    for (const auto& item : self.config->items) {
        if (item.identifier == key) {
            return &item;
        }
    }
    return nullptr;
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarDefaultItemIdentifiers:(NSToolbar*)toolbar {
    (void)toolbar;
    NSMutableArray<NSToolbarItemIdentifier>* result = [NSMutableArray array];
    if (!self.config) {
        return result;
    }
    if (!self.config->default_item_identifiers.empty()) {
        for (const auto& id : self.config->default_item_identifiers) {
            [result addObject:[NSString stringWithUTF8String:id.c_str()]];
        }
    } else {
        for (const auto& item : self.config->items) {
            [result addObject:[NSString stringWithUTF8String:item.identifier.c_str()]];
        }
    }
    return result;
}

- (NSArray<NSToolbarItemIdentifier>*)toolbarAllowedItemIdentifiers:(NSToolbar*)toolbar {
    (void)toolbar;
    NSMutableArray<NSToolbarItemIdentifier>* result = [NSMutableArray array];
    if (!self.config) {
        return result;
    }
    for (const auto& item : self.config->items) {
        [result addObject:[NSString stringWithUTF8String:item.identifier.c_str()]];
    }
    [result addObject:NSToolbarFlexibleSpaceItemIdentifier];
    [result addObject:NSToolbarSpaceItemIdentifier];
    if (@available(macOS 11.0, *)) {
        [result addObject:NSToolbarSidebarTrackingSeparatorItemIdentifier];
    }
    return result;
}

- (NSToolbarItem*)toolbar:(NSToolbar*)toolbar
        itemForItemIdentifier:(NSToolbarItemIdentifier)itemIdentifier
    willBeInsertedIntoToolbar:(BOOL)flag {
    (void)toolbar;
    (void)flag;
    const nk::NativeToolbarItem* desc = [self itemWithIdentifier:itemIdentifier];
    if (desc == nullptr) {
        return nil;
    }

    NSString* label = [NSString stringWithUTF8String:desc->label.c_str()];
    NSString* tooltip = [NSString stringWithUTF8String:desc->tooltip.c_str()];

    switch (desc->kind) {
    case nk::NativeToolbarItem::Kind::Separator:
    case nk::NativeToolbarItem::Kind::Space: {
        NSToolbarItem* item = [[NSToolbarItem alloc] initWithItemIdentifier:itemIdentifier];
        [item setLabel:label];
        return item;
    }
    case nk::NativeToolbarItem::Kind::FlexibleSpace: {
        NSToolbarItem* item = [[NSToolbarItem alloc] initWithItemIdentifier:itemIdentifier];
        [item setLabel:label];
        return item;
    }
    case nk::NativeToolbarItem::Kind::SearchField: {
        if (@available(macOS 11.0, *)) {
            NSSearchToolbarItem* search =
                [[NSSearchToolbarItem alloc] initWithItemIdentifier:itemIdentifier];
            [search setLabel:label];
            [search setToolTip:tooltip];
            [[search searchField] setDelegate:self];
            [[search searchField] setTarget:self];
            [[search searchField] setAction:@selector(searchFieldSubmit:)];
            return search;
        }
        NSToolbarItem* fallback = [[NSToolbarItem alloc] initWithItemIdentifier:itemIdentifier];
        NSSearchField* field = [[NSSearchField alloc] initWithFrame:NSMakeRect(0, 0, 180, 22)];
        [field setDelegate:self];
        [field setTarget:self];
        [field setAction:@selector(searchFieldSubmit:)];
        [fallback setView:field];
        [fallback setLabel:label];
        [fallback setToolTip:tooltip];
        return fallback;
    }
    case nk::NativeToolbarItem::Kind::Button: {
        NSToolbarItem* item = [[NSToolbarItem alloc] initWithItemIdentifier:itemIdentifier];
        [item setLabel:label];
        [item setPaletteLabel:label];
        [item setToolTip:tooltip];
        [item setTarget:self];
        [item setAction:@selector(toolbarAction:)];

        NSImage* image = nil;
        if (!desc->symbol_name.empty()) {
            if (@available(macOS 11.0, *)) {
                NSString* symbol = [NSString stringWithUTF8String:desc->symbol_name.c_str()];
                image = [NSImage imageWithSystemSymbolName:symbol accessibilityDescription:label];
            }
        }
        if (image != nil) {
            [item setImage:image];
        }
        return item;
    }
    }
    return nil;
}

- (void)toolbarAction:(NSToolbarItem*)sender {
    if (sender == nil) {
        return;
    }
    const nk::NativeToolbarItem* desc = [self itemWithIdentifier:[sender itemIdentifier]];
    if (desc != nullptr && desc->on_activate) {
        desc->on_activate();
    }
}

- (void)searchFieldSubmit:(id)sender {
    NSSearchField* field = nil;
    if ([sender isKindOfClass:[NSSearchField class]]) {
        field = static_cast<NSSearchField*>(sender);
    } else {
        return;
    }
    if (!self.config) {
        return;
    }
    NSString* raw_value = [field stringValue];
    NSString* value = raw_value != nil ? raw_value : @"";
    const char* utf8 = [value UTF8String];
    std::string_view text_view{utf8 != nullptr ? utf8 : ""};
    for (const auto& item : self.config->items) {
        if (item.kind == nk::NativeToolbarItem::Kind::SearchField && item.on_search_submit) {
            item.on_search_submit(text_view);
            break;
        }
    }
}

@end

// ---------------------------------------------------------------------------
// MacosSurface implementation
// ---------------------------------------------------------------------------

namespace nk {

namespace {

void apply_titlebar_style(NSWindow* window, TitlebarStyle style) {
    NSWindowStyleMask mask = [window styleMask];
    switch (style) {
    case TitlebarStyle::Regular:
        mask &= ~NSWindowStyleMaskFullSizeContentView;
        [window setTitlebarAppearsTransparent:NO];
        [window setTitleVisibility:NSWindowTitleVisible];
        break;
    case TitlebarStyle::Unified:
        mask |= NSWindowStyleMaskFullSizeContentView;
        [window setTitlebarAppearsTransparent:YES];
        [window setTitleVisibility:NSWindowTitleVisible];
        break;
    case TitlebarStyle::Hidden:
        mask |= NSWindowStyleMaskFullSizeContentView;
        [window setTitlebarAppearsTransparent:YES];
        [window setTitleVisibility:NSWindowTitleHidden];
        break;
    }
    [window setStyleMask:mask];
}

} // namespace

MacosSurface::MacosSurface(const WindowConfig& config, Window& owner) : owner_(owner) {
    @autoreleasepool {
        NSUInteger style_mask =
            NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
        if (config.resizable) {
            style_mask |= NSWindowStyleMaskResizable;
        }

        NSRect content_rect = NSMakeRect(0, 0, config.width, config.height);
        window_ = [[NSWindow alloc] initWithContentRect:content_rect
                                              styleMask:style_mask
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
        [window_ setTitle:[NSString stringWithUTF8String:config.title.c_str()]];
        [window_ center];
        [window_ setReleasedWhenClosed:NO];

        view_ = [[NKView alloc] initWithFrame:content_rect surface:this];
        [window_ setContentView:view_];

        window_delegate_ = [[NKWindowDelegate alloc] init];
        window_delegate_.surface = this;
        [window_ setDelegate:window_delegate_];

        apply_titlebar_style(window_, config.titlebar_style);
    }
}

MacosSurface::~MacosSurface() {
    @autoreleasepool {
        if (toolbar_) {
            [toolbar_ setDelegate:nil];
            if (window_) {
                [window_ setToolbar:nil];
            }
            toolbar_ = nil;
        }
        if (toolbar_delegate_) {
            toolbar_delegate_ = nil;
        }
        if (window_) {
            [window_ setDelegate:nil];
            [window_ close];
            window_ = nil;
        }
        if (view_) {
            static_cast<NKView*>(view_).surface = nullptr;
            view_ = nil;
        }
        if (window_delegate_) {
            window_delegate_.surface = nullptr;
            window_delegate_ = nil;
        }
    }
    toolbar_config_.reset();
}

void MacosSurface::show() {
    @autoreleasepool {
        [window_ makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    }
}

void MacosSurface::hide() {
    @autoreleasepool {
        [window_ orderOut:nil];
    }
}

void MacosSurface::set_title(std::string_view title) {
    @autoreleasepool {
        [window_ setTitle:[NSString stringWithUTF8String:std::string(title).c_str()]];
    }
}

void MacosSurface::resize(int width, int height) {
    @autoreleasepool {
        [window_ setContentSize:NSMakeSize(width, height)];
    }
}

Size MacosSurface::size() const {
    @autoreleasepool {
        NSRect frame = [[window_ contentView] frame];
        return {static_cast<float>(frame.size.width), static_cast<float>(frame.size.height)};
    }
}

Insets MacosSurface::content_insets() const {
    @autoreleasepool {
        if (window_ == nil || ([window_ styleMask] & NSWindowStyleMaskFullSizeContentView) == 0) {
            return {};
        }
        // With a full-size content view the titlebar/toolbar chrome overlaps
        // the top of the content view; contentLayoutRect excludes it.
        const NSRect frame = [[window_ contentView] frame];
        const NSRect layout = [window_ contentLayoutRect];
        const float top = static_cast<float>(std::max(0.0, frame.size.height - NSMaxY(layout)));
        return {top, 0.0F, 0.0F, 0.0F};
    }
}

float MacosSurface::scale_factor() const {
    @autoreleasepool {
        if (window_ != nullptr) {
            CGFloat scale = window_.backingScaleFactor;
            if (scale > 0.0) {
                return static_cast<float>(scale);
            }
        }
        return 1.0F;
    }
}

RendererBackendSupport MacosSurface::renderer_backend_support() const {
    return {
        .software = true,
        .d3d11 = false,
        .metal = true,
        .open_gl = false,
        .vulkan = false,
    };
}

void MacosSurface::present(const uint8_t* rgba,
                           int w,
                           int h,
                           std::span<const Rect> damage_regions) {
    const size_t byte_count = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
    const bool size_changed =
        pixel_width_ != w || pixel_height_ != h || pixel_buffer_.size() != byte_count;
    pixel_buffer_.resize(byte_count);

    auto copy_region = [&](Rect rect) {
        const int x0 = std::max(0, static_cast<int>(std::floor(rect.x)));
        const int y0 = std::max(0, static_cast<int>(std::floor(rect.y)));
        const int x1 = std::min(w, static_cast<int>(std::ceil(rect.right())));
        const int y1 = std::min(h, static_cast<int>(std::ceil(rect.bottom())));
        if (x1 <= x0 || y1 <= y0) {
            return;
        }

        for (int y = y0; y < y1; ++y) {
            const auto row_offset = static_cast<size_t>((y * w + x0) * 4);
            const auto row_bytes = static_cast<size_t>(x1 - x0) * 4;
            std::memcpy(pixel_buffer_.data() + row_offset, rgba + row_offset, row_bytes);
        }
    };

    if (size_changed || damage_regions.empty()) {
        std::memcpy(pixel_buffer_.data(), rgba, byte_count);
    } else {
        for (const auto& rect : damage_regions) {
            copy_region(rect);
        }
    }
    pixel_width_ = w;
    pixel_height_ = h;

    @autoreleasepool {
        if (size_changed || damage_regions.empty()) {
            [view_ setNeedsDisplay:YES];
            return;
        }

        const float scale = std::max(0.0001F, scale_factor());
        for (const auto& rect : damage_regions) {
            const NSRect dirty =
                NSMakeRect(rect.x / scale, rect.y / scale, rect.width / scale, rect.height / scale);
            [view_ setNeedsDisplayInRect:dirty];
        }
    }
}

void MacosSurface::sync_native_state(bool moved) {
    @autoreleasepool {
        [view_ synchronizeNativeState:moved ? YES : NO];
    }
}

void MacosSurface::set_fullscreen(bool fullscreen) {
    if (fullscreen_ != fullscreen) {
        @autoreleasepool {
            [window_ toggleFullScreen:nil];
        }
        fullscreen_ = fullscreen;
    }
}

bool MacosSurface::is_fullscreen() const {
    return fullscreen_;
}

void MacosSurface::minimize() {
    @autoreleasepool {
        [window_ performMiniaturize:nil];
    }
}

void MacosSurface::toggle_maximize() {
    @autoreleasepool {
        [window_ zoom:nil];
    }
}

bool MacosSurface::is_maximized() const {
    @autoreleasepool {
        return window_ != nil && [window_ isZoomed];
    }
}

NativeWindowHandle MacosSurface::native_handle() const {
    return (__bridge NativeWindowHandle)window_;
}

NativeWindowHandle MacosSurface::native_display_handle() const {
    return nullptr;
}

void MacosSurface::set_cursor_shape(CursorShape shape) {
    @autoreleasepool {
        switch (shape) {
        case CursorShape::IBeam:
            [[NSCursor IBeamCursor] set];
            break;
        case CursorShape::PointingHand:
            [[NSCursor pointingHandCursor] set];
            break;
        case CursorShape::ResizeLeftRight:
            [[NSCursor resizeLeftRightCursor] set];
            break;
        case CursorShape::ResizeUpDown:
            [[NSCursor resizeUpDownCursor] set];
            break;
        case CursorShape::Default:
        default:
            [[NSCursor arrowCursor] set];
            break;
        }
    }
}

void MacosSurface::set_titlebar_style(TitlebarStyle style) {
    @autoreleasepool {
        if (window_) {
            apply_titlebar_style(window_, style);
        }
    }
}

void MacosSurface::set_native_toolbar(const NativeToolbarConfig* config) {
    @autoreleasepool {
        if (config == nullptr) {
            if (toolbar_ && window_) {
                [window_ setToolbar:nil];
            }
            if (toolbar_) {
                [toolbar_ setDelegate:nil];
                toolbar_ = nil;
            }
            toolbar_delegate_ = nil;
            toolbar_config_.reset();
            return;
        }

        toolbar_config_ = std::make_unique<NativeToolbarConfig>(*config);

        if (toolbar_delegate_ == nil) {
            toolbar_delegate_ = [[NKToolbarDelegate alloc] init];
        }
        toolbar_delegate_.config = toolbar_config_.get();

        NSString* raw_identifier =
            [NSString stringWithUTF8String:toolbar_config_->identifier.c_str()];
        NSString* identifier = raw_identifier != nil ? raw_identifier : @"NodalKitToolbar";

        NSToolbar* new_toolbar = [[NSToolbar alloc] initWithIdentifier:identifier];
        [new_toolbar setDelegate:toolbar_delegate_];
        [new_toolbar
            setAllowsUserCustomization:toolbar_config_->allows_user_customization ? YES : NO];
        [new_toolbar setAutosavesConfiguration:YES];
        // Icon-only matches the modern macOS toolbar default; labels remain
        // available through tooltips and the customization palette.
        [new_toolbar setDisplayMode:NSToolbarDisplayModeIconOnly];

        toolbar_ = new_toolbar;
        if (window_) {
            [window_ setToolbar:toolbar_];
        }
    }
}

} // namespace nk
