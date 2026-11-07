/// @file model_test.cpp
/// @brief Tests for concrete and abstract tree and table model contracts.

#include <catch2/catch_test_macros.hpp>
#include <nk/model/abstract_table_model.h>
#include <nk/model/abstract_tree_model.h>
#include <nk/model/list_model_adapters.h>
#include <nk/model/tree_model.h>
#include <utility>
#include <vector>

TEST_CASE("SortListModel extracts keys once and keeps equal keys in source order", "[model]") {
    const auto long_key = std::string(200, 'Z');
    auto source = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{"Beta", "Alpha", "Beta", "", "Alpha", long_key, "Ω"});
    nk::SortListModel sorted{source};
    std::vector<std::size_t> calls(source->row_count());
    auto key = [&](const nk::AbstractListModel& model, std::size_t row) {
        ++calls.at(row);
        return model.display_text(row);
    };
    auto check_order = [&](std::vector<std::size_t> expected) {
        REQUIRE(sorted.row_count() == expected.size());
        for (std::size_t row = 0; row < expected.size(); ++row) {
            REQUIRE(sorted.map_to_source(row) == expected[row]);
            REQUIRE(sorted.map_from_source(expected[row]) == row);
            REQUIRE(sorted.display_text(row) == source->display_text(expected[row]));
            REQUIRE(std::any_cast<std::string>(sorted.data(row)) == source->at(expected[row]));
        }
    };

    sorted.set_sort_key(key);
    REQUIRE(sorted.has_sort_key());
    REQUIRE_FALSE(sorted.has_less());
    REQUIRE(calls == std::vector<std::size_t>(7, 1));
    check_order({3, 1, 4, 0, 2, 5, 6});

    sorted.set_sort_key(key, nk::ListModelSortDirection::Descending);
    REQUIRE(calls == std::vector<std::size_t>(7, 2));
    check_order({6, 5, 0, 2, 1, 4, 3});

    sorted.set_less([](const nk::AbstractListModel& model, std::size_t lhs, std::size_t rhs) {
        return model.display_text(lhs) < model.display_text(rhs);
    });
    REQUIRE(sorted.has_less());
    REQUIRE_FALSE(sorted.has_sort_key());
    check_order({3, 1, 4, 0, 2, 5, 6});
    REQUIRE(calls == std::vector<std::size_t>(7, 2));

    sorted.set_sort_key(key);
    REQUIRE_FALSE(sorted.has_less());
    REQUIRE(calls == std::vector<std::size_t>(7, 3));
    sorted.set_sort_key({});
    REQUIRE_FALSE(sorted.has_sort_key());
    check_order({0, 1, 2, 3, 4, 5, 6});
    sorted.set_sort_key(key);
    sorted.set_less({});
    REQUIRE_FALSE(sorted.has_sort_key());
    check_order({0, 1, 2, 3, 4, 5, 6});
}

TEST_CASE("SortListModel refreshes keys and mappings before notifying observers", "[model]") {
    class MutableModel final : public nk::StringListModel {
    public:
        using nk::StringListModel::StringListModel;

        std::string display_text(std::size_t row) const override {
            return changed && row == 0 ? "Aardvark" : nk::StringListModel::display_text(row);
        }

        void rename_first() {
            changed = true;
            notify_data_changed(0, 0);
        }

        bool changed = false;
    };

    auto source = std::make_shared<MutableModel>(std::vector<std::string>{"Gamma", "Alpha"});
    nk::SortListModel sorted{source};
    std::size_t calls = 0;
    auto key = [&](const nk::AbstractListModel& model, std::size_t row) {
        ++calls;
        return model.display_text(row);
    };
    sorted.set_sort_key(key);
    std::size_t resets = 0;
    auto connection = sorted.on_model_reset().connect([&] {
        ++resets;
        const auto* model = sorted.source_model();
        REQUIRE(sorted.row_count() == (model ? model->row_count() : 0));
        for (std::size_t row = 1; row < sorted.row_count(); ++row) {
            REQUIRE(sorted.display_text(row - 1) <= sorted.display_text(row));
        }
    });
    (void)connection;

    calls = 0;
    source->append("Beta");
    REQUIRE(calls == 3);
    REQUIRE(resets == 1);
    REQUIRE(sorted.map_to_source(2) == 0);
    calls = 0;
    source->rename_first();
    REQUIRE(calls == 3);
    REQUIRE(resets == 2);
    REQUIRE(sorted.map_to_source(0) == 0);
    calls = 0;
    source->insert(0, "Ignored");
    REQUIRE(calls == 4);
    REQUIRE(resets == 3);
    calls = 0;
    source->remove(1);
    REQUIRE(calls == 3);
    REQUIRE(resets == 4);
    REQUIRE_FALSE(sorted.map_from_source(3));
    calls = 0;
    source->clear();
    REQUIRE(calls == 0);
    REQUIRE(resets == 5);
    REQUIRE(sorted.row_count() == 0);

    auto replacement =
        std::make_shared<nk::StringListModel>(std::vector<std::string>{"Zulu", "Alpha"});
    sorted.set_source_model(replacement);
    REQUIRE(calls == 2);
    REQUIRE(resets == 6);
    REQUIRE(sorted.map_to_source(0) == 1);
    source->append("Detached");
    REQUIRE(calls == 2);
    REQUIRE(resets == 6);
    sorted.set_source_model(nullptr);
    REQUIRE(sorted.row_count() == 0);
    REQUIRE(resets == 7);
}

class ConcreteTableModel : public nk::AbstractTableModel {
public:
    std::size_t row_count() const override { return rows_; }

    std::size_t column_count() const override { return cols_; }

    std::any data(std::size_t, std::size_t) const override { return {}; }

    void trigger_insert_rows(std::size_t first, std::size_t count) {
        begin_insert_rows(first, count);
        rows_ += count;
        end_insert_rows();
    }

    void trigger_insert_cols(std::size_t first, std::size_t count) {
        begin_insert_columns(first, count);
        cols_ += count;
        end_insert_columns();
    }

    void trigger_remove_rows(std::size_t first, std::size_t count) {
        begin_remove_rows(first, count);
        rows_ -= count;
        end_remove_rows();
    }

    void trigger_remove_cols(std::size_t first, std::size_t count) {
        begin_remove_columns(first, count);
        cols_ -= count;
        end_remove_columns();
    }

    void
    trigger_data_changed(std::size_t top, std::size_t left, std::size_t bottom, std::size_t right) {
        notify_data_changed(top, left, bottom, right);
    }

    void trigger_reset() {
        rows_ = 0;
        cols_ = 0;
        notify_model_reset();
    }

private:
    std::size_t rows_ = 0;
    std::size_t cols_ = 0;
};

class ConcreteTreeModel : public nk::AbstractTreeModel {
public:
    std::size_t children_count(const nk::TreeIndex&) const override { return 0; }

    nk::TreeIndex child(const nk::TreeIndex&, std::size_t) const override { return {}; }

    nk::TreeIndex parent(const nk::TreeIndex&) const override { return {}; }

    std::size_t row_of(const nk::TreeIndex&) const override { return 0; }

    std::any data(const nk::TreeIndex&) const override { return {}; }

    void trigger_insert(const nk::TreeIndex& parent, std::size_t first, std::size_t count) {
        begin_insert_nodes(parent, first, count);
        end_insert_nodes();
    }

    void trigger_remove(const nk::TreeIndex& parent, std::size_t first, std::size_t count) {
        begin_remove_nodes(parent, first, count);
        end_remove_nodes();
    }

    void trigger_data_changed(const nk::TreeIndex& parent, std::size_t first, std::size_t last) {
        notify_data_changed(parent, first, last);
    }

    void trigger_reset() { notify_model_reset(); }
};

TEST_CASE("AbstractTableModel emits row change signals", "[model]") {
    ConcreteTableModel table;

    std::size_t about_first = 0, about_count = 0;
    auto c1 = table.on_rows_about_to_insert().connect([&](std::size_t f, std::size_t c) {
        about_first = f;
        about_count = c;
    });

    std::size_t done_first = 0, done_count = 0;
    auto c2 = table.on_rows_inserted().connect([&](std::size_t f, std::size_t c) {
        done_first = f;
        done_count = c;
    });

    table.trigger_insert_rows(2, 3);

    REQUIRE(about_first == 2);
    REQUIRE(about_count == 3);
    REQUIRE(done_first == 2);
    REQUIRE(done_count == 3);
}

TEST_CASE("AbstractTableModel emits column change signals", "[model]") {
    ConcreteTableModel table;

    std::size_t about_first = 0, about_count = 0;
    auto c1 = table.on_columns_about_to_insert().connect([&](std::size_t f, std::size_t c) {
        about_first = f;
        about_count = c;
    });

    std::size_t done_first = 0, done_count = 0;
    auto c2 = table.on_columns_inserted().connect([&](std::size_t f, std::size_t c) {
        done_first = f;
        done_count = c;
    });

    table.trigger_insert_cols(1, 5);

    REQUIRE(about_first == 1);
    REQUIRE(about_count == 5);
    REQUIRE(done_first == 1);
    REQUIRE(done_count == 5);
}

TEST_CASE("AbstractTreeModel emits node change signals", "[model]") {
    ConcreteTreeModel tree;

    nk::TreeIndex test_parent{reinterpret_cast<void*>(0x1234), 5};

    nk::TreeIndex about_parent;
    std::size_t about_first = 0, about_count = 0;
    auto c1 =
        tree.on_nodes_about_to_insert().connect([&](nk::TreeIndex p, std::size_t f, std::size_t c) {
            about_parent = p;
            about_first = f;
            about_count = c;
        });

    nk::TreeIndex done_parent;
    std::size_t done_first = 0, done_count = 0;
    auto c2 = tree.on_nodes_inserted().connect([&](nk::TreeIndex p, std::size_t f, std::size_t c) {
        done_parent = p;
        done_first = f;
        done_count = c;
    });

    tree.trigger_insert(test_parent, 0, 10);

    REQUIRE(about_parent == test_parent);
    REQUIRE(about_first == 0);
    REQUIRE(about_count == 10);
    REQUIRE(done_parent == test_parent);
    REQUIRE(done_first == 0);
    REQUIRE(done_count == 10);
}

TEST_CASE("AbstractTableModel emits row removal signals", "[model]") {
    ConcreteTableModel table;
    table.trigger_insert_rows(0, 5);

    std::size_t about_first = 0, about_count = 0;
    auto c1 = table.on_rows_about_to_remove().connect([&](std::size_t f, std::size_t c) {
        about_first = f;
        about_count = c;
    });

    std::size_t done_first = 0, done_count = 0;
    auto c2 = table.on_rows_removed().connect([&](std::size_t f, std::size_t c) {
        done_first = f;
        done_count = c;
    });

    table.trigger_remove_rows(1, 2);

    REQUIRE(about_first == 1);
    REQUIRE(about_count == 2);
    REQUIRE(done_first == 1);
    REQUIRE(done_count == 2);
    REQUIRE(table.row_count() == 3);
}

TEST_CASE("AbstractTableModel emits column removal signals", "[model]") {
    ConcreteTableModel table;
    table.trigger_insert_cols(0, 4);

    std::size_t about_first = 0, about_count = 0;
    auto c1 = table.on_columns_about_to_remove().connect([&](std::size_t f, std::size_t c) {
        about_first = f;
        about_count = c;
    });

    std::size_t done_first = 0, done_count = 0;
    auto c2 = table.on_columns_removed().connect([&](std::size_t f, std::size_t c) {
        done_first = f;
        done_count = c;
    });

    table.trigger_remove_cols(0, 3);

    REQUIRE(about_first == 0);
    REQUIRE(about_count == 3);
    REQUIRE(done_first == 0);
    REQUIRE(done_count == 3);
    REQUIRE(table.column_count() == 1);
}

TEST_CASE("AbstractTableModel emits data change and reset signals", "[model]") {
    ConcreteTableModel table;
    table.trigger_insert_rows(0, 3);
    table.trigger_insert_cols(0, 3);

    std::size_t top = 0, left = 0, bottom = 0, right = 0;
    auto c1 = table.on_data_changed().connect(
        [&](std::size_t t, std::size_t l, std::size_t b, std::size_t r) {
            top = t;
            left = l;
            bottom = b;
            right = r;
        });

    table.trigger_data_changed(1, 0, 2, 2);

    REQUIRE(top == 1);
    REQUIRE(left == 0);
    REQUIRE(bottom == 2);
    REQUIRE(right == 2);

    int reset_count = 0;
    auto c2 = table.on_model_reset().connect([&] { reset_count++; });

    table.trigger_reset();

    REQUIRE(reset_count == 1);
    REQUIRE(table.row_count() == 0);
    REQUIRE(table.column_count() == 0);
}

TEST_CASE("AbstractTreeModel emits node removal signals", "[model]") {
    ConcreteTreeModel tree;

    nk::TreeIndex test_parent{reinterpret_cast<void*>(0x1234), 5};

    nk::TreeIndex about_parent;
    std::size_t about_first = 0, about_count = 0;
    auto c1 =
        tree.on_nodes_about_to_remove().connect([&](nk::TreeIndex p, std::size_t f, std::size_t c) {
            about_parent = p;
            about_first = f;
            about_count = c;
        });

    nk::TreeIndex done_parent;
    std::size_t done_first = 0, done_count = 0;
    auto c2 = tree.on_nodes_removed().connect([&](nk::TreeIndex p, std::size_t f, std::size_t c) {
        done_parent = p;
        done_first = f;
        done_count = c;
    });

    tree.trigger_remove(test_parent, 2, 4);

    REQUIRE(about_parent == test_parent);
    REQUIRE(about_first == 2);
    REQUIRE(about_count == 4);
    REQUIRE(done_parent == test_parent);
    REQUIRE(done_first == 2);
    REQUIRE(done_count == 4);
}

TEST_CASE("AbstractTreeModel emits data change and reset signals", "[model]") {
    ConcreteTreeModel tree;

    nk::TreeIndex test_parent{nullptr, 7};

    nk::TreeIndex changed_parent;
    std::size_t changed_first = 0, changed_last = 0;
    auto c1 = tree.on_data_changed().connect([&](nk::TreeIndex p, std::size_t f, std::size_t l) {
        changed_parent = p;
        changed_first = f;
        changed_last = l;
    });

    tree.trigger_data_changed(test_parent, 1, 3);

    REQUIRE(changed_parent == test_parent);
    REQUIRE(changed_first == 1);
    REQUIRE(changed_last == 3);

    int reset_count = 0;
    auto c2 = tree.on_model_reset().connect([&] { reset_count++; });

    tree.trigger_reset();

    REQUIRE(reset_count == 1);
}

TEST_CASE("TreeModel keeps both visible mappings coherent across mutations", "[model][tree]") {
    nk::TreeModel model;
    const auto check_rows = [&](std::vector<nk::TreeNodeId> expected) {
        REQUIRE(model.visible_row_count() == expected.size());
        for (std::size_t row = 0; row < expected.size(); ++row) {
            CHECK(model.visible_node_at(row) == expected[row]);
            CHECK(model.visible_row_for_node(expected[row]) == row);
        }
        CHECK_FALSE(model.visible_row_for_node(nk::InvalidTreeNodeId));
        CHECK_FALSE(model.visible_row_for_node(1000000));
    };
    check_rows({});
    const auto first = model.add_root("first");
    const auto child = model.append_child(first, "child");
    const auto grandchild = model.append_child(child, "grandchild");
    const auto last = model.add_root("last");
    check_rows({first, child, grandchild, last});
    model.set_expanded(first, false);
    check_rows({first, last});
    CHECK_FALSE(model.visible_row_for_node(child));
    CHECK_FALSE(model.visible_row_for_node(grandchild));
    const auto hidden_child = model.append_child(child, "hidden child");
    check_rows({first, last});
    CHECK_FALSE(model.visible_row_for_node(hidden_child));
    model.set_expanded(child, false);
    model.set_expanded(first, true);
    check_rows({first, child, last});
    model.toggle_expanded(child);
    check_rows({first, child, grandchild, hidden_child, last});
    const auto sibling = model.append_child(first, "sibling");
    check_rows({first, child, grandchild, hidden_child, sibling, last});
    model.set_display_text(child, "renamed");
    check_rows({first, child, grandchild, hidden_child, sibling, last});
    CHECK(model.display_text(child) == "renamed");
    model.clear();
    check_rows({});
    CHECK_FALSE(model.visible_row_for_node(child));
    const auto reused = model.add_root("reused");
    CHECK(reused == 0);
    check_rows({reused});
    CHECK_FALSE(model.visible_row_for_node(child));
}

TEST_CASE("TreeModel reset observers see the updated visible index", "[model][tree][signal]") {
    nk::TreeModel model;
    std::vector<std::vector<nk::TreeNodeId>> snapshots;
    auto connection = model.on_model_reset().connect([&] {
        std::vector<nk::TreeNodeId> rows;
        for (std::size_t row = 0; row < model.visible_row_count(); ++row) {
            const auto id = model.visible_node_at(row);
            CHECK(model.visible_row_for_node(id) == row);
            rows.push_back(id);
        }
        snapshots.push_back(std::move(rows));
    });
    const auto root = model.add_root("root");
    const auto child = model.append_child(root, "child");
    model.set_expanded(root, false);
    const auto hidden = model.append_child(root, "hidden");
    model.set_expanded(root, true);
    model.set_display_text(child, "renamed");
    model.clear();
    CHECK(snapshots == std::vector<std::vector<nk::TreeNodeId>>{{root},
                                                                {root, child},
                                                                {root},
                                                                {root},
                                                                {root, child, hidden},
                                                                {root, child, hidden},
                                                                {}});
    CHECK(connection.connected());
}

TEST_CASE("TreeModel indexes deeply nested expanded nodes", "[model][tree]") {
    nk::TreeModel model;
    const auto root = model.add_root("root");
    auto leaf = root;
    constexpr std::size_t depth = 20000;
    for (std::size_t level = 1; level < depth; ++level) {
        leaf = model.append_child(leaf, "child");
    }
    CHECK(model.visible_row_count() == depth);
    CHECK(model.visible_node_at(depth - 1) == leaf);
    CHECK(model.visible_row_for_node(leaf) == depth - 1);
    model.set_expanded(root, false);
    CHECK(model.visible_row_count() == 1);
    CHECK_FALSE(model.visible_row_for_node(leaf));
    model.set_expanded(root, true);
    CHECK(model.visible_node_at(depth - 1) == leaf);
}
