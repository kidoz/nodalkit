/// @file macos_window.mm
/// @brief macOS Cocoa native surface implementation.

#include "macos_window.h"

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#include <cstring>
#include <nk/accessibility/accessible.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/ui_core/widget.h>
#include <string_view>

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

static bool debug_node_is_accessible(const nk::WidgetDebugNode& node) {
    return !node.accessible_hidden && !node.accessible_role.empty() &&
           node.accessible_role != "none";
}

static NSString* ns_string(std::string_view value) {
    return [NSString stringWithUTF8String:std::string(value).c_str()];
}

static bool debug_type_matches(const nk::WidgetDebugNode& node, std::string_view suffix) {
    return node.type_name == suffix ||
           (node.type_name.size() > suffix.size() &&
            node.type_name.ends_with(std::string("::") + std::string(suffix)));
}

static NSString* macos_accessibility_role(const nk::WidgetDebugNode& node) {
    if (debug_type_matches(node, "ComboBox")) {
        return NSAccessibilityPopUpButtonRole;
    }
    const std::string_view role = node.accessible_role;
    if (role == "button" || role == "togglebutton") {
        return NSAccessibilityButtonRole;
    }
    if (role == "checkbox") {
        return NSAccessibilityCheckBoxRole;
    }
    if (role == "dialog" || role == "tabpanel" || role == "window") {
        return NSAccessibilityGroupRole;
    }
    if (role == "grid") {
        return NSAccessibilityTableRole;
    }
    if (role == "gridcell") {
        return NSAccessibilityCellRole;
    }
    if (role == "image") {
        return NSAccessibilityImageRole;
    }
    if (role == "label") {
        return NSAccessibilityStaticTextRole;
    }
    if (role == "link") {
        return NSAccessibilityLinkRole;
    }
    if (role == "list") {
        return NSAccessibilityListRole;
    }
    if (role == "listitem" || role == "treeitem") {
        return NSAccessibilityRowRole;
    }
    if (role == "menu") {
        return NSAccessibilityMenuRole;
    }
    if (role == "menubar") {
        return NSAccessibilityMenuBarRole;
    }
    if (role == "menuitem") {
        return NSAccessibilityMenuItemRole;
    }
    if (role == "progressbar") {
        return NSAccessibilityProgressIndicatorRole;
    }
    if (role == "radiobutton") {
        return NSAccessibilityRadioButtonRole;
    }
    if (role == "scrollbar") {
        return NSAccessibilityScrollBarRole;
    }
    if (role == "slider" || role == "spinbutton") {
        return NSAccessibilitySliderRole;
    }
    if (role == "tab" || role == "tablist") {
        return NSAccessibilityTabGroupRole;
    }
    if (role == "textinput") {
        return NSAccessibilityTextFieldRole;
    }
    if (role == "toolbar") {
        return NSAccessibilityToolbarRole;
    }
    if (role == "tree") {
        return NSAccessibilityOutlineRole;
    }
    return NSAccessibilityGroupRole;
}

static NSString* macos_accessibility_label(const nk::WidgetDebugNode& node) {
    if (!node.accessible_name.empty()) {
        return ns_string(node.accessible_name);
    }
    if (!node.debug_name.empty()) {
        return ns_string(node.debug_name);
    }
    if (!node.type_name.empty()) {
        return ns_string(node.type_name);
    }
    return @"widget";
}

static NSString* macos_accessibility_title(const nk::WidgetDebugNode& node) {
    return macos_accessibility_label(node);
}

static bool macos_accessibility_enabled(const nk::WidgetDebugNode& node) {
    return node.sensitive &&
           (node.accessible_state & nk::StateFlags::Disabled) == nk::StateFlags::None;
}

static nk::Widget* resolve_widget_by_tree_path(nk::Window& window,
                                               std::span<const std::size_t> tree_path) {
    nk::Widget* current = window.child();
    if (current == nullptr) {
        return nullptr;
    }
    for (const auto index : tree_path) {
        const auto children = current->children();
        if (index >= children.size() || children[index] == nullptr) {
            return nullptr;
        }
        current = children[index].get();
    }
    return current;
}

// ---------------------------------------------------------------------------
// NKView — custom NSView for rendering and input
// ---------------------------------------------------------------------------

@class NKAccessibilityNode;

@interface NKView : NSView
@property(nonatomic, assign) nk::MacosSurface* surface;
@end

@interface NKAccessibilityNode : NSAccessibilityElement
- (instancetype)initWithNode:(const nk::WidgetDebugNode&)node parent:(id)parent view:(NKView*)view;
- (BOOL)matchesFocusedState;
@end

static NSArray<id>*
build_accessibility_children(const nk::WidgetDebugNode& root, id parent, NKView* view);
static id accessibility_hit_test(NSArray<id>* elements, NSPoint point);
static id accessibility_focused_element(NSArray<id>* elements);

@implementation NKView {
    NSTrackingArea* tracking_area_;
}

- (instancetype)initWithFrame:(NSRect)frame surface:(nk::MacosSurface*)surface {
    self = [super initWithFrame:frame];
    if (self) {
        _surface = surface;
        [self updateTrackingAreas];
    }
    return self;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)isFlipped {
    // Use top-left origin to match nk coordinate system.
    return YES;
}

- (BOOL)isAccessibilityElement {
    return NO;
}

- (NSString*)accessibilityRole {
    return NSAccessibilityGroupRole;
}

- (NSString*)accessibilityLabel {
    return @"NodalKit window content";
}

- (NSString*)accessibilityTitle {
    return @"NodalKit window content";
}

- (NSArray<id>*)accessibilityChildren {
    if (!_surface) {
        return @[];
    }
    return build_accessibility_children(_surface->owner().debug_tree(), self, self);
}

- (id)accessibilityFocusedUIElement {
    id focused = accessibility_focused_element(self.accessibilityChildren);
    return focused != nil ? focused : self;
}

- (id)accessibilityHitTest:(NSPoint)point {
    id hit = accessibility_hit_test(self.accessibilityChildren, point);
    return hit != nil ? hit : self;
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
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Press;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.button = 1;
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
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
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
}

- (void)rightMouseDown:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Press;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.button = 2;
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
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
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
}

- (void)otherMouseDown:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    NSPoint loc = [self convertPoint:event.locationInWindow fromView:nil];
    nk::MouseEvent me{};
    me.type = nk::MouseEvent::Type::Press;
    me.x = static_cast<float>(loc.x);
    me.y = static_cast<float>(loc.y);
    me.button = macos_button_number(event);
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
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
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
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
    me.modifiers = macos_modifiers(event.modifierFlags);
    _surface->owner().dispatch_mouse_event(me);
}

// -- Keyboard events --

- (void)keyDown:(NSEvent*)event {
    if (!_surface) {
        return;
    }
    nk::KeyEvent ke{};
    ke.type = nk::KeyEvent::Type::Press;
    ke.key = macos_keycode_to_nk(event.keyCode);
    ke.modifiers = macos_modifiers(event.modifierFlags);
    ke.is_repeat = event.isARepeat;
    _surface->owner().dispatch_key_event(ke);
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

@implementation NKAccessibilityNode {
    nk::WidgetDebugNode node_;
    id parent_;
    NKView* view_;
    NSArray<id>* children_;
}

- (instancetype)initWithNode:(const nk::WidgetDebugNode&)node parent:(id)parent view:(NKView*)view {
    self = [super init];
    if (self) {
        node_ = node;
        parent_ = parent;
        view_ = view;
        children_ = build_accessibility_children(node_, self, view);
    }
    return self;
}

- (BOOL)isAccessibilityElement {
    return YES;
}

- (id)accessibilityParent {
    return parent_;
}

- (NSArray<id>*)accessibilityChildren {
    return children_;
}

- (NSString*)accessibilityRole {
    return macos_accessibility_role(node_);
}

- (NSString*)accessibilityLabel {
    return macos_accessibility_label(node_);
}

- (NSString*)accessibilityTitle {
    return macos_accessibility_title(node_);
}

- (NSString*)accessibilityHelp {
    if (node_.accessible_description.empty()) {
        return nil;
    }
    return ns_string(node_.accessible_description);
}

- (id)accessibilityValue {
    if (!node_.accessible_value.empty()) {
        return ns_string(node_.accessible_value);
    }
    if (node_.accessible_role == "label" || debug_type_matches(node_, "Label")) {
        return macos_accessibility_label(node_);
    }
    if (debug_type_matches(node_, "ComboBox") && !node_.accessible_name.empty()) {
        return ns_string(node_.accessible_name);
    }
    return nil;
}

- (BOOL)accessibilityEnabled {
    return macos_accessibility_enabled(node_);
}

- (NSRect)accessibilityFrame {
    if (view_ == nil || view_.window == nil) {
        return NSZeroRect;
    }
    const auto& allocation = node_.allocation;
    NSRect local_rect = NSMakeRect(static_cast<CGFloat>(allocation.x),
                                   static_cast<CGFloat>(allocation.y),
                                   static_cast<CGFloat>(allocation.width),
                                   static_cast<CGFloat>(allocation.height));
    NSRect window_rect = [view_ convertRect:local_rect toView:nil];
    return [view_.window convertRectToScreen:window_rect];
}

- (id)accessibilityFocusedUIElement {
    id focused = accessibility_focused_element(children_);
    return focused != nil ? focused : (node_.focused ? self : nil);
}

- (id)accessibilityHitTest:(NSPoint)point {
    id hit = accessibility_hit_test(children_, point);
    if (hit != nil) {
        return hit;
    }
    return NSPointInRect(point, self.accessibilityFrame) ? self : nil;
}

- (BOOL)matchesFocusedState {
    return node_.focused;
}

- (BOOL)accessibilityPerformPress {
    if (view_ == nil || view_.surface == nullptr) {
        return NO;
    }
    auto* widget = resolve_widget_by_tree_path(view_.surface->owner(), node_.tree_path);
    if (widget == nullptr || widget->accessible() == nullptr) {
        return NO;
    }
    if (widget->accessible()->supports_action(nk::AccessibleAction::Activate)) {
        return widget->accessible()->perform_action(nk::AccessibleAction::Activate) ? YES : NO;
    }
    if (widget->accessible()->supports_action(nk::AccessibleAction::Toggle)) {
        return widget->accessible()->perform_action(nk::AccessibleAction::Toggle) ? YES : NO;
    }
    if (widget->accessible()->supports_action(nk::AccessibleAction::Focus)) {
        return widget->accessible()->perform_action(nk::AccessibleAction::Focus) ? YES : NO;
    }
    return NO;
}

@end

static void append_accessibility_descendants(const nk::WidgetDebugNode& node,
                                             id parent,
                                             NKView* view,
                                             NSMutableArray<id>* out) {
    if (debug_node_is_accessible(node)) {
        [out addObject:[[NKAccessibilityNode alloc] initWithNode:node parent:parent view:view]];
        return;
    }
    for (const auto& child : node.children) {
        append_accessibility_descendants(child, parent, view, out);
    }
}

static NSArray<id>*
build_accessibility_children(const nk::WidgetDebugNode& root, id parent, NKView* view) {
    NSMutableArray<id>* children = [NSMutableArray array];
    for (const auto& child : root.children) {
        append_accessibility_descendants(child, parent, view, children);
    }
    return children;
}

static id accessibility_hit_test(NSArray<id>* elements, NSPoint point) {
    for (id candidate in [elements reverseObjectEnumerator]) {
        if (![candidate isKindOfClass:[NKAccessibilityNode class]]) {
            continue;
        }
        id nested = [candidate accessibilityHitTest:point];
        if (nested != nil) {
            return nested;
        }
    }
    return nil;
}

static id accessibility_focused_element(NSArray<id>* elements) {
    for (id candidate in elements) {
        if (![candidate isKindOfClass:[NKAccessibilityNode class]]) {
            continue;
        }
        if ([(NKAccessibilityNode*)candidate matchesFocusedState]) {
            return candidate;
        }
        id nested = accessibility_focused_element([candidate accessibilityChildren]);
        if (nested != nil) {
            return nested;
        }
    }
    return nil;
}

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
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
    if (!_surface) {
        return;
    }
    nk::WindowEvent we{};
    we.type = nk::WindowEvent::Type::FocusIn;
    _surface->owner().dispatch_window_event(we);
}

- (void)windowDidResignKey:(NSNotification*)notification {
    if (!_surface) {
        return;
    }
    nk::WindowEvent we{};
    we.type = nk::WindowEvent::Type::FocusOut;
    _surface->owner().dispatch_window_event(we);
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
}

@end

// ---------------------------------------------------------------------------
// MacosSurface implementation
// ---------------------------------------------------------------------------

namespace nk {

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
    }
}

MacosSurface::~MacosSurface() {
    @autoreleasepool {
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

} // namespace nk
