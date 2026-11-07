#include <cassert>
#include <memory>
#include <nk/model/tree_model.h>
#include <span>
#include <utility>
#include <vector>

namespace nk {

struct TreeModel::VisibleIndex {
    std::vector<TreeNodeId> nodes;
    std::vector<std::size_t> rows;
    bool dirty = true;
};

TreeModel::TreeModel() : visible_index_(std::make_unique<VisibleIndex>()) {}

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
    visible_index_->dirty = true;
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
    visible_index_->dirty = true;
    model_reset_.emit();
    return id;
}

void TreeModel::clear() {
    if (nodes_.empty() && roots_.empty()) {
        return;
    }
    nodes_.clear();
    roots_.clear();
    *visible_index_ = VisibleIndex{};
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
    visible_index_->dirty = true;
    model_reset_.emit();
}

void TreeModel::toggle_expanded(TreeNodeId node_id) {
    set_expanded(node_id, !is_expanded(node_id));
}

bool TreeModel::is_expanded(TreeNodeId node_id) const {
    return node(node_id).expanded;
}

std::size_t TreeModel::visible_row_count() const {
    return visible_index().nodes.size();
}

TreeNodeId TreeModel::visible_node_at(std::size_t row) const {
    const auto& visible = visible_index().nodes;
    assert(row < visible.size());
    return visible[row];
}

std::optional<std::size_t> TreeModel::visible_row_for_node(TreeNodeId node_id) const {
    if (!contains(node_id)) {
        return std::nullopt;
    }
    const auto row = visible_index().rows[node_id];
    return row == InvalidTreeNodeId ? std::nullopt : std::optional(row);
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

const TreeModel::VisibleIndex& TreeModel::visible_index() const {
    auto& index = *visible_index_;
    if (!index.dirty) {
        return index;
    }
    index.nodes.clear();
    index.nodes.reserve(nodes_.size());
    index.rows.assign(nodes_.size(), InvalidTreeNodeId);

    // Keep traversal storage proportional to depth, without recursive stack growth.
    struct Level {
        std::span<const TreeNodeId> siblings;
        std::size_t next = 0;
    };

    std::vector<Level> levels{{roots_}};
    while (!levels.empty()) {
        auto& level = levels.back();
        if (level.next == level.siblings.size()) {
            levels.pop_back();
            continue;
        }
        const auto id = level.siblings[level.next++];
        index.rows[id] = index.nodes.size();
        index.nodes.push_back(id);
        const auto& item = node(id);
        if (item.expanded && !item.children.empty()) {
            levels.push_back({item.children});
        }
    }
    index.dirty = false;
    return index;
}

} // namespace nk
