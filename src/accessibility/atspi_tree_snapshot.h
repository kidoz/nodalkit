#pragma once

#include "accessibility_tree.h"

#include <nk/accessibility/atspi_bridge.h>
#include <nk/foundation/types.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nk::detail {

struct AtspiTreeNode {
    AtspiAccessibleNode node;
    AccessibleId id = 0; ///< Zero for the window node.
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
                                                                Rect window_bounds);

// AT-SPI text offsets count characters. `end` < 0 means the end of the text.
[[nodiscard]] int atspi_character_count(std::string_view text);
[[nodiscard]] std::string atspi_text_slice(std::string_view text, int start, int end);

[[nodiscard]] std::optional<AccessibleAction> atspi_action_from_name(std::string_view name);

} // namespace nk::detail
