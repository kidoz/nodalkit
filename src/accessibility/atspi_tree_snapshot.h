#pragma once

#include "accessibility_tree.h"

#include <array>
#include <cstdint>
#include <nk/accessibility/atspi_bridge.h>
#include <nk/foundation/types.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nk::detail {

// AtspiRole values used on the wire (libatspi atspi-constants.h).
enum class AtspiRoleValue : std::uint32_t {
    CheckBox = 7,
    ComboBox = 11,
    Dialog = 16,
    Frame = 23,
    Image = 27,
    Label = 29,
    List = 31,
    ListItem = 32,
    Menu = 33,
    MenuBar = 34,
    MenuItem = 35,
    PageTab = 37,
    PageTabList = 38,
    Panel = 39,
    ProgressBar = 42,
    PushButton = 43,
    RadioButton = 44,
    ScrollBar = 48,
    Separator = 50,
    Slider = 51,
    SpinButton = 52,
    StatusBar = 54,
    Table = 55,
    TableCell = 56,
    Text = 61,
    ToggleButton = 62,
    ToolBar = 63,
    Tree = 65,
    Unknown = 67,
    Application = 75,
    Entry = 79,
    Link = 88,
    TreeItem = 91,
};

// AtspiStateType bit positions within the two-word state set.
enum class AtspiStateValue : std::uint32_t {
    Active = 1,
    Checked = 4,
    Editable = 7,
    Enabled = 8,
    Expandable = 9,
    Expanded = 10,
    Focusable = 11,
    Focused = 12,
    MultiLine = 17,
    Pressed = 20,
    Selectable = 22,
    Selected = 23,
    Sensitive = 24,
    Showing = 25,
    SingleLine = 26,
    Visible = 30,
    Checkable = 41,
};

using AtspiStateSet = std::array<std::uint32_t, 2>;

void add_atspi_state(AtspiStateSet& set, AtspiStateValue state);
[[nodiscard]] bool has_atspi_state(const AtspiStateSet& set, AtspiStateValue state);

struct AtspiTreeNode {
    AtspiAccessibleNode node{};
    AccessibleId id = 0; ///< Zero for the window node.
    AtspiRoleValue role = AtspiRoleValue::Unknown;
    AtspiStateSet states{};
    std::string accessible_id{}; ///< Widget debug name, for UI automation.
};

// AT-SPI view of one window built from stable accessible identities. The first
// node is the window; widget object paths embed the identity, so a path an
// assistive technology holds keeps naming the same widget while the tree
// changes, and disappears when that widget is gone. Exposure, modality, and
// enabled state follow AccessibilityTree. UI thread only.
[[nodiscard]] std::vector<AtspiTreeNode> build_atspi_tree_nodes(AccessibilityTree& tree,
                                                                std::string_view subtree_root_path,
                                                                std::string_view window_object_name,
                                                                std::string_view window_title,
                                                                Rect window_bounds,
                                                                bool window_active = false);

// One AT-SPI event signal. `data` is the signal's variant argument: an int32
// zero, a string, or an object reference within this application.
struct AtspiEvent {
    enum class Data : std::uint8_t { None, Text, Object };

    std::string object_path{};
    std::string interface_name{};
    std::string member{};
    std::string detail{};
    int detail1 = 0;
    int detail2 = 0;
    Data data = Data::None;
    std::string data_value{};

    bool operator==(const AtspiEvent&) const = default;
};

// Events that turn the `before` snapshot into `after`, both including the
// application root and every window. Children changes come first, then states
// that turn off (so focus leaves before it arrives), states that turn on, names,
// and text. Text changes carry the minimal inserted or deleted span, so a typed
// character is announced alone rather than the whole field.
[[nodiscard]] std::vector<AtspiEvent> diff_atspi_nodes(const std::vector<AtspiTreeNode>& before,
                                                       const std::vector<AtspiTreeNode>& after);

// AT-SPI text offsets count characters. `end` < 0 means the end of the text.
[[nodiscard]] int atspi_character_count(std::string_view text);
[[nodiscard]] std::string atspi_text_slice(std::string_view text, int start, int end);

[[nodiscard]] std::optional<AccessibleAction> atspi_action_from_name(std::string_view name);

} // namespace nk::detail
