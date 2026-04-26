/// @file property_test.cpp
/// @brief Tests for Property<T> and bindings.

#include <catch2/catch_test_macros.hpp>
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
