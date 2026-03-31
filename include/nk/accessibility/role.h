#pragma once

/// @file role.h
/// @brief Accessibility roles for widgets.

#include <cstdint>

namespace nk {

/// Semantic role of a widget for accessibility purposes.
/// Modeled after WAI-ARIA roles.
enum class AccessibleRole : uint8_t {
    None,
    Button,
    CheckBox,
    Dialog,
    Grid,
    GridCell,
    Image,
    Label,
    Link,
    List,
    ListItem,
    Menu,
    MenuBar,
    MenuItem,
    ProgressBar,
    RadioButton,
    ScrollBar,
    Separator,
    Slider,
    SpinButton,
    Tab,
    TabList,
    TabPanel,
    TextInput,
    ToggleButton,
    Toolbar,
    Tree,
    TreeItem,
    Window,
};

} // namespace nk
