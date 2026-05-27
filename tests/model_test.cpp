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
};

TEST_CASE("AbstractTableModel emits row change signals", "[model]") {
    ConcreteTableModel table;
    
    std::size_t about_first = 0, about_count = 0;
    auto c1 = table.on_rows_about_to_insert().connect([&](std::size_t f, std::size_t c) {
        about_first = f; about_count = c;
    });

    std::size_t done_first = 0, done_count = 0;
    auto c2 = table.on_rows_inserted().connect([&](std::size_t f, std::size_t c) {
        done_first = f; done_count = c;
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
        about_first = f; about_count = c;
    });

    std::size_t done_first = 0, done_count = 0;
    auto c2 = table.on_columns_inserted().connect([&](std::size_t f, std::size_t c) {
        done_first = f; done_count = c;
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
    auto c1 = tree.on_nodes_about_to_insert().connect([&](nk::TreeIndex p, std::size_t f, std::size_t c) {
        about_parent = p; about_first = f; about_count = c;
    });

    nk::TreeIndex done_parent;
    std::size_t done_first = 0, done_count = 0;
    auto c2 = tree.on_nodes_inserted().connect([&](nk::TreeIndex p, std::size_t f, std::size_t c) {
        done_parent = p; done_first = f; done_count = c;
    });

    tree.trigger_insert(test_parent, 0, 10);
    
    REQUIRE(about_parent == test_parent);
    REQUIRE(about_first == 0);
    REQUIRE(about_count == 10);
    REQUIRE(done_parent == test_parent);
    REQUIRE(done_first == 0);
    REQUIRE(done_count == 10);
}