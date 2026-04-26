/// @file signal_test.cpp
/// @brief Tests for Signal, Connection, and ScopedConnection.

#include <catch2/catch_test_macros.hpp>
#include <nk/foundation/signal.h>
#include <string>

TEST_CASE("Signal emits to connected slots", "[signal]") {
    nk::Signal<int> sig;
    int received = 0;

    auto conn = sig.connect([&](const int& v) { received = v; });
    sig.emit(42);

    REQUIRE(received == 42);
    REQUIRE(conn.connected());
    REQUIRE(sig.connection_count() == 1);
}

TEST_CASE("Signal disconnect stops later emissions", "[signal]") {
    nk::Signal<> sig;
    int call_count = 0;

    auto conn = sig.connect([&] { ++call_count; });
    sig.emit();
    REQUIRE(call_count == 1);

    conn.disconnect();
    sig.emit();
    REQUIRE(call_count == 1);
    REQUIRE_FALSE(conn.connected());
}

TEST_CASE("ScopedConnection disconnects on destruction", "[signal]") {
    nk::Signal<> sig;
    int call_count = 0;

    {
        nk::ScopedConnection sc(sig.connect([&] { ++call_count; }));
        sig.emit();
        REQUIRE(call_count == 1);
    }

    sig.emit();
    REQUIRE(call_count == 1);
}

TEST_CASE("Signal supports multiple independent slots", "[signal]") {
    nk::Signal<std::string> sig;
    std::string a;
    std::string b;

    auto c1 = sig.connect([&](const std::string& s) { a = s; });
    auto c2 = sig.connect([&](const std::string& s) { b = s; });
    sig.emit("hello");

    REQUIRE(a == "hello");
    REQUIRE(b == "hello");
    REQUIRE(sig.connection_count() == 2);

    c1.disconnect();
    sig.emit("world");
    REQUIRE(a == "hello");
    REQUIRE(b == "world");
}

TEST_CASE("Signal allows self-disconnect during emission", "[signal]") {
    nk::Signal<> sig;
    nk::Connection conn;
    int call_count = 0;

    conn = sig.connect([&] {
        ++call_count;
        conn.disconnect(); // disconnect self during emission
    });

    sig.emit();
    REQUIRE(call_count == 1);

    sig.emit();
    REQUIRE(call_count == 1);
}

TEST_CASE("Signal disconnect_all removes every slot", "[signal]") {
    nk::Signal<> sig;
    int a = 0;
    int b = 0;

    auto conn_a = sig.connect([&] { ++a; });
    auto conn_b = sig.connect([&] { ++b; });
    (void)conn_a;
    (void)conn_b;
    sig.emit();
    REQUIRE(a == 1);
    REQUIRE(b == 1);

    sig.disconnect_all();
    sig.emit();
    REQUIRE(a == 1);
    REQUIRE(b == 1);
    REQUIRE(sig.connection_count() == 0);
}
