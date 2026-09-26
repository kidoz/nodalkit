#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <nk/accessibility/accessible.h>
#include <nk/foundation/types.h>
#include <nk/ui_core/state_flags.h>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace nk {
class Widget;
class Window;
} // namespace nk

namespace nk::detail {

using AccessibleId = std::uint64_t;

// Attributes of one exposed element, copied out of the live widget.
struct AccessibleNodeInfo {
    AccessibleRole role = AccessibleRole::None;
    std::string type_name;
    std::string debug_name;
    std::string name;
    std::string description;
    std::string value;
    Rect bounds{};
    StateFlags state = StateFlags::None;
    bool enabled = true;
    bool focusable = false;
    bool focused = false;
    std::vector<AccessibleAction> actions;
};

// Platform-neutral structure behind native accessibility bridges. UI thread only.
//
// Identity: each widget keeps one id for its whole lifetime. Ids are keyed by the
// widget's control block, which the weak_ptr held here keeps allocated, so a
// destroyed widget's id is never reused for a new widget at the same address.
//
// Exposure: an element is a widget with a role other than None whose whole
// ancestor chain is visible and not hidden from assistive technology, rooted in
// the window content or an overlay. While a modal overlay is shown, only the
// topmost modal overlay is exposed, so nothing behind it can be reached or
// activated. Containers without a role are flattened into their parent.
//
// Everything is read live from the widget tree on each call; nothing is cached
// beyond identities, so answers never describe a stale tree.
class AccessibilityTree {
public:
    explicit AccessibilityTree(Window& window);
    ~AccessibilityTree() = default;

    AccessibilityTree(const AccessibilityTree&) = delete;
    AccessibilityTree& operator=(const AccessibilityTree&) = delete;
    AccessibilityTree(AccessibilityTree&&) = delete;
    AccessibilityTree& operator=(AccessibilityTree&&) = delete;

    [[nodiscard]] std::vector<AccessibleId> root_children();
    [[nodiscard]] std::vector<AccessibleId> children(AccessibleId id);
    [[nodiscard]] bool is_exposed(AccessibleId id) const;
    // Nearest exposed ancestor; nullopt at the window root or when not exposed.
    [[nodiscard]] std::optional<AccessibleId> parent(AccessibleId id);
    [[nodiscard]] std::optional<AccessibleNodeInfo> info(AccessibleId id) const;
    // Exposed element that contains the focused widget.
    [[nodiscard]] std::optional<AccessibleId> focused();
    // Deepest exposed element under a point in window coordinates.
    [[nodiscard]] std::optional<AccessibleId> hit_test(Point point);

    // Actions run only on exposed, enabled elements.
    bool perform(AccessibleId id, AccessibleAction action);
    bool focus(AccessibleId id);

    // Forget identities of destroyed widgets and return them, so bridges can
    // announce and release their native elements.
    std::vector<AccessibleId> purge();

private:
    [[nodiscard]] Widget* exposed_widget(AccessibleId id) const;
    AccessibleId id_for(Widget& widget);
    void collect(Widget& widget, std::vector<AccessibleId>& out);
    std::optional<AccessibleId> hit_test(Widget& widget, Point point);

    Window& window_;
    std::map<std::weak_ptr<Widget>, AccessibleId, std::owner_less<>> ids_;
    std::unordered_map<AccessibleId, std::weak_ptr<Widget>> widgets_;
    AccessibleId next_id_ = 1;
};

} // namespace nk::detail
