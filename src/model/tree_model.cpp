#include <algorithm>
#include <cassert>
#include <nk/model/tree_model.h>
#include <utility>

namespace nk {

TreeModel::TreeModel() = default;

TreeModel::~TreeModel() = default;

TreeNodeId TreeModel::add_root(std::string text) {
    const TreeNodeId id = nodes_.size();
    nodes_.push_back(Node{
        .id = id,
        .parent = InvalidTreeNodeId,
        .text = std::move(text),
        .children = {},
    });
    roots_.push_back(id);
    model_reset_.emit();
    return id;
}

TreeNodeId TreeModel::append_child(TreeNodeId parent, std::string text) {
    assert(contains(parent));
    const TreeNodeId id = nodes_.size();
    nodes_.push_back(Node{
        .id = id,
        .parent = parent,
        .text = std::move(text),
        .children = {},
    });
    node(parent).children.push_back(id);
    model_reset_.emit();
    return id;
}

void TreeModel::clear() {
    if (nodes_.empty() && roots_.empty()) {
        return;
    }
    nodes_.clear();
    roots_.clear();
    model_reset_.emit();
}

bool TreeModel::contains(TreeNodeId node_id) const {
    return node_id < nodes_.size() && nodes_[node_id].id == node_id;
}

std::string_view TreeModel::display_text(TreeNodeId node_id) const {
    return node(node_id).text;
}

void TreeModel::set_display_text(TreeNodeId node_id, std::string text) {
    node(node_id).text = std::move(text);
    model_reset_.emit();
}

TreeNodeId TreeModel::parent(TreeNodeId node_id) const {
    return node(node_id).parent;
}

std::span<const TreeNodeId> TreeModel::children(TreeNodeId node_id) const {
    return node(node_id).children;
}

std::span<const TreeNodeId> TreeModel::roots() const {
    return roots_;
}

bool TreeModel::has_children(TreeNodeId node_id) const {
    return !node(node_id).children.empty();
}

std::size_t TreeModel::depth(TreeNodeId node_id) const {
    std::size_t value = 0;
    TreeNodeId parent_id = parent(node_id);
    while (parent_id != InvalidTreeNodeId) {
        ++value;
        parent_id = parent(parent_id);
    }
    return value;
}

void TreeModel::set_expanded(TreeNodeId node_id, bool expanded) {
    auto& item = node(node_id);
    if (item.expanded == expanded) {
        return;
    }
    item.expanded = expanded;
    model_reset_.emit();
}

void TreeModel::toggle_expanded(TreeNodeId node_id) {
    set_expanded(node_id, !is_expanded(node_id));
}

bool TreeModel::is_expanded(TreeNodeId node_id) const {
    return node(node_id).expanded;
}

std::size_t TreeModel::visible_row_count() const {
    return visible_nodes().size();
}

TreeNodeId TreeModel::visible_node_at(std::size_t row) const {
    const auto visible = visible_nodes();
    assert(row < visible.size());
    return visible[row];
}

std::optional<std::size_t> TreeModel::visible_row_for_node(TreeNodeId node_id) const {
    const auto visible = visible_nodes();
    const auto iter = std::find(visible.begin(), visible.end(), node_id);
    if (iter == visible.end()) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(std::distance(visible.begin(), iter));
}

Signal<>& TreeModel::on_model_reset() {
    return model_reset_;
}

const TreeModel::Node& TreeModel::node(TreeNodeId id) const {
    assert(contains(id));
    return nodes_[id];
}

TreeModel::Node& TreeModel::node(TreeNodeId id) {
    assert(contains(id));
    return nodes_[id];
}

void TreeModel::append_visible_nodes(TreeNodeId node_id, std::vector<TreeNodeId>& visible) const {
    visible.push_back(node_id);
    const auto& item = node(node_id);
    if (!item.expanded) {
        return;
    }
    for (const auto child : item.children) {
        append_visible_nodes(child, visible);
    }
}

std::vector<TreeNodeId> TreeModel::visible_nodes() const {
    std::vector<TreeNodeId> visible;
    visible.reserve(nodes_.size());
    for (const auto root : roots_) {
        append_visible_nodes(root, visible);
    }
    return visible;
}

} // namespace nk
