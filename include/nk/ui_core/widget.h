#pragma once

/// @file widget.h
/// @brief Base class for all visual elements in the widget tree.

#include <nk/foundation/signal.h>
#include <nk/foundation/types.h>
#include <nk/layout/constraints.h>
#include <nk/ui_core/cursor_shape.h>
#include <nk/ui_core/state_flags.h>

// Forward declarations for snapshot pipeline.
namespace nk {
class SnapshotContext;
}

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

class EventController;
struct FontDescriptor;
class LayoutManager;
struct KeyEvent;
struct MouseEvent;
class Window;

/// Child layout policy for each axis.
enum class SizePolicy {
    Fixed,
    Preferred,
    Expanding,
};

/// Base class for all widgets. Widgets form a tree with unique-pointer
/// ownership: a parent exclusively owns its children.
///
/// Key responsibilities:
/// - Tree structure (parent / children)
/// - Layout (measure / allocate)
/// - State flags (hovered, pressed, focused, disabled, ...)
/// - Style class management
/// - Invalidation (redraw / layout)
/// - Controller attachment
///
/// Widgets are created via factory functions returning shared_ptr so that
/// both the parent tree and the caller can hold a reference during setup.
class Widget : public std::enable_shared_from_this<Widget> {
public:
    virtual ~Widget();

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    // --- Tree ---

    /// The parent widget, or nullptr for root / unparented.
    [[nodiscard]] Widget* parent() const;

    /// Ordered list of children.
    [[nodiscard]] std::span<const std::shared_ptr<Widget>> children() const;

    // --- Visibility / Sensitivity ---

    [[nodiscard]] bool is_visible() const;
    void set_visible(bool visible);

    [[nodiscard]] bool is_sensitive() const;
    void set_sensitive(bool sensitive);

    // --- State ---

    [[nodiscard]] StateFlags state_flags() const;

    // --- Style classes ---

    void add_style_class(std::string_view name);
    void remove_style_class(std::string_view name);
    [[nodiscard]] bool has_style_class(std::string_view name) const;

    // --- Layout ---

    /// Measure this widget under the given constraints.
    [[nodiscard]] virtual SizeRequest measure(const Constraints& constraints) const;

    /// Allocate a position and size for this widget.
    virtual void allocate(const Rect& allocation);

    /// The rectangle allocated by the parent.
    [[nodiscard]] const Rect& allocation() const;

    /// Set a layout manager that controls how children are arranged.
    void set_layout_manager(std::unique_ptr<LayoutManager> lm);
    [[nodiscard]] LayoutManager* layout_manager() const;

    /// Preferred sizing policy on the horizontal axis.
    [[nodiscard]] SizePolicy horizontal_size_policy() const;
    void set_horizontal_size_policy(SizePolicy policy);

    /// Preferred sizing policy on the vertical axis.
    [[nodiscard]] SizePolicy vertical_size_policy() const;
    void set_vertical_size_policy(SizePolicy policy);

    /// Stretch factor used by layouts that distribute surplus space.
    [[nodiscard]] uint8_t horizontal_stretch() const;
    void set_horizontal_stretch(uint8_t stretch);

    /// Stretch factor used by layouts that distribute surplus space.
    [[nodiscard]] uint8_t vertical_stretch() const;
    void set_vertical_stretch(uint8_t stretch);

    /// Whether hidden widgets should continue reserving layout space.
    [[nodiscard]] bool retain_size_when_hidden() const;
    void set_retain_size_when_hidden(bool retain);

    // --- Focus ---

    [[nodiscard]] bool is_focusable() const;
    void set_focusable(bool focusable);
    void grab_focus();

    // --- Invalidation ---

    void queue_redraw();
    void queue_layout();

    // --- Controllers ---

    void add_controller(std::shared_ptr<EventController> controller);

    // --- Signals ---

    Signal<>& on_map();
    Signal<>& on_unmap();
    Signal<>& on_destroy();

    /// Produce render nodes into the snapshot context.
    /// Default implementation recursively snapshots children.
    /// Subclasses override to produce their own render nodes.
    virtual void snapshot(SnapshotContext& ctx) const;

    /// Handle a mouse event targeted at this widget.
    /// Returns true if the event was consumed.
    virtual bool handle_mouse_event(const MouseEvent& event);

    /// Handle a key event targeted at this widget.
    /// Returns true if the event was consumed.
    virtual bool handle_key_event(const KeyEvent& event);

    /// Whether the widget wants to receive pointer events at the given point.
    /// Widgets with transient popup content may override this to extend the
    /// interactive region beyond their base allocation.
    [[nodiscard]] virtual bool hit_test(Point point) const;

    /// Cursor shape hint to use while the pointer is over this widget.
    [[nodiscard]] virtual CursorShape cursor_shape() const;

    /// Called when window focus routing changes this widget's focus state.
    virtual void on_focus_changed(bool focused);

protected:
    Widget();

    /// Resolve a themed color for the widget from style rules or semantic
    /// tokens. Falls back when the property is unset or has a different type.
    [[nodiscard]] Color theme_color(std::string_view property_name, Color fallback) const;

    /// Resolve a themed numeric metric for the widget from style rules or
    /// semantic tokens. Falls back when the property is unset or has a
    /// different type.
    [[nodiscard]] float theme_number(std::string_view property_name, float fallback) const;

    /// Measure text using the host window's active text shaper when
    /// available. Falls back to a simple estimate otherwise.
    [[nodiscard]] Size measure_text(std::string_view text, const FontDescriptor& font) const;

    /// Container widgets call these to manage children.
    void append_child(std::shared_ptr<Widget> child);
    void insert_child(std::size_t index, std::shared_ptr<Widget> child);
    void remove_child(Widget& child);
    void clear_children();

    /// Set state flags (called by controllers).
    void set_state_flag(StateFlags flag, bool active);

private:
    friend class Window;

    void set_host_window(Window* window);
    void dispatch_pointer_controllers(const MouseEvent& event);
    void dispatch_keyboard_controllers(const KeyEvent& event);
    void dispatch_focus_controllers(bool focused);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
