/// @file model_test.cpp
/// @brief Tests for AbstractTableModel and AbstractTreeModel base capabilities.

#include <catch2/catch_test_macros.hpp>
#include <nk/model/abstract_table_model.h>
#include <nk/model/abstract_tree_model.h>

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
