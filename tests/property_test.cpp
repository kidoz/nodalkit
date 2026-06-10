/// @file property_test.cpp
/// @brief Tests for Property<T> and bindings.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <limits>
#include <nk/foundation/property.h>
#include <string>

TEST_CASE("Property get and set update the value", "[property]") {
    nk::Property<int> prop{10};
    REQUIRE(prop.get() == 10);

    prop.set(20);
    REQUIRE(prop.get() == 20);
}

TEST_CASE("Property emits only when the value changes", "[property]") {
    nk::Property<std::string> prop{"hello"};
    std::string last;

    auto conn = prop.on_changed().connect([&](const std::string& v) { last = v; });

    prop.set("world");
    REQUIRE(last == "world");

    last.clear();
    prop.set("world");
    REQUIRE(last.empty());
}

TEST_CASE("Property supports implicit read access", "[property]") {
    nk::Property<int> prop{42};
    int v = prop;
    REQUIRE(v == 42);
}

TEST_CASE("Property one-way binding tracks the source", "[property]") {
    nk::Property<int> source{1};
    nk::Property<int> target{0};

    auto binding = target.bind_to(source);

    REQUIRE(target.get() == 1);

    source.set(99);
    REQUIRE(target.get() == 99);

    binding.disconnect();
    source.set(200);
    REQUIRE(target.get() == 99);
}

TEST_CASE("Property two-way binding synchronizes both ways", "[property]") {
    nk::Property<int> a{10};
    nk::Property<int> b{20};

    auto binding = a.bind_bidirectional(b);

    // Initial state: a should be updated to b's value
    REQUIRE(a.get() == 20);
    REQUIRE(b.get() == 20);

    // Change a -> b should update
    a.set(30);
    REQUIRE(b.get() == 30);
    REQUIRE(a.get() == 30);

    // Change b -> a should update
    b.set(40);
    REQUIRE(a.get() == 40);
    REQUIRE(b.get() == 40);
}

TEST_CASE("Property two-way binding terminates on self-unequal values", "[property]") {
    nk::Property<double> a{0.0};
    nk::Property<double> b{0.0};

    auto binding = a.bind_bidirectional(b);

    int emit_count = 0;
    auto conn = a.on_changed().connect([&](const double&) { emit_count++; });

    // Without the self-unequal guard this recursed forever: NaN != NaN keeps
    // both sides emitting until the stack overflows.
    a.set(std::numeric_limits<double>::quiet_NaN());
    REQUIRE(std::isnan(a.get()));
    REQUIRE(std::isnan(b.get()));
    REQUIRE(emit_count == 1);

    // Setting NaN over NaN is treated as unchanged.
    a.set(std::numeric_limits<double>::quiet_NaN());
    REQUIRE(emit_count == 1);

    // A real value still propagates after the NaN episode.
    a.set(1.5);
    REQUIRE(b.get() == 1.5);
    REQUIRE(emit_count == 2);
}
