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

#include <array>
#include <atomic>
#include <nk/runtime/event_loop.h>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <vector>

TEST_CASE("Signal posts cross-thread emissions to the target EventLoop", "[signal][thread]") {
    nk::EventLoop loop;
    nk::Signal<int> sig;
    std::atomic<int> received{0};

    auto conn = sig.connect([&](int v) { received = v; });

    std::thread bg([&] { sig.emit(42); });
    bg.join();

    REQUIRE(received == 0);

    loop.poll();
    REQUIRE(received == 42);
}

TEST_CASE("Signal cancels queued deliveries when disconnected or destroyed", "[signal][thread]") {
    nk::EventLoop loop;
    auto signal = std::make_unique<nk::Signal<int>>();
    auto receiver = std::make_unique<int>(0);
    auto conn = signal->connect([value = receiver.get()](int v) { *value = v; });
    std::thread worker([&] { signal->emit(42); });
    worker.join();
    SECTION("Explicit disconnection") {
        conn.disconnect();
    }
    SECTION("Signal destruction") {
        signal.reset();
    }
    SECTION("Move assignment discards the old connections") {
        *signal = nk::Signal<int>{};
    }
    REQUIRE_FALSE(conn.connected());
    receiver.reset();
    loop.poll();
}

TEST_CASE("Queued signals own string and span views", "[signal][thread]") {
    nk::EventLoop loop;
    nk::Signal<std::string_view, std::span<const int>> signal;
    std::string received;
    std::vector<int> numbers;
    auto conn = signal.connect([&](std::string_view text, std::span<const int> values) {
        received = text;
        numbers.assign(values.begin(), values.end());
    });
    std::thread worker([&] {
        std::string text(1000, 'x');
        std::array values{1, 2, 3};
        signal.emit(text, values);
        text.assign(1000, 'y');
        values.fill(0);
    });
    worker.join();
    loop.poll();
    REQUIRE(received == std::string(1000, 'x'));
    REQUIRE(numbers == std::vector<int>{1, 2, 3});
}

TEST_CASE("Signal delivery tolerates a destroyed target loop", "[signal][thread]") {
    nk::Signal<int> signal;
    int calls = 0;
    nk::Connection conn;
    {
        nk::EventLoop loop;
        conn = signal.connect([&](int) { ++calls; });
    }
    std::thread worker([&] { signal.emit(42); });
    worker.join();
    REQUIRE(calls == 0);
}

TEST_CASE("Cross-thread mutable references are rejected instead of invoked on the worker",
          "[signal][thread]") {
    nk::EventLoop loop;
    nk::Signal<int&> signal;
    int calls = 0;
    auto conn = signal.connect([&](int&) { ++calls; });
    bool rejected = false;
    std::thread worker([&] {
        int value = 1;
        try {
            signal.emit(value);
        } catch (const std::logic_error&) {
            rejected = true;
        }
    });
    worker.join();
    loop.poll();
    REQUIRE(rejected);
    REQUIRE(calls == 0);
}
