/// @file event_loop_test.cpp
/// @brief Tests for EventLoop timers, posted tasks, and reentrancy safety.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <nk/runtime/event_loop.h>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

TEST_CASE("One-shot timers fire once per schedule", "[event_loop]") {
    nk::EventLoop loop;
    int fired = 0;

    (void)loop.set_timeout(0ms, [&] { ++fired; }, "test-timeout");
    loop.poll();
    loop.poll();
    REQUIRE(fired == 1);
}

TEST_CASE("Interval timers re-arm after each fire and stop when cancelled", "[event_loop]") {
    nk::EventLoop loop;
    int ticks = 0;

    // A zero interval is indistinguishable from a one-shot, so exercise a
    // real interval here. The first poll runs before the interval elapses.
    auto handle = loop.set_interval(1ms, [&] { ++ticks; }, "test-interval");
    loop.poll();
    std::this_thread::sleep_for(2ms);
    loop.poll();
    std::this_thread::sleep_for(2ms);
    loop.poll();
    REQUIRE(ticks == 2);

    loop.cancel(handle);
    std::this_thread::sleep_for(2ms);
    loop.poll();
    REQUIRE(ticks == 2);
}

TEST_CASE("Scheduling timers from a timer callback does not corrupt the timer list",
          "[event_loop]") {
    nk::EventLoop loop;
    std::vector<int> fired;

    // Pad the list with far-future timers so the callbacks appending timers
    // are likely to cross a vector growth boundary mid-poll.
    for (int i = 0; i < 100; ++i) {
        (void)loop.set_timeout(1h, [] {}, "pad");
    }
    (void)loop.set_timeout(
        0ms,
        [&] {
            fired.push_back(1);
            for (int i = 0; i < 200; ++i) {
                (void)loop.set_timeout(1h, [] {}, "scheduled-from-callback");
            }
            (void)loop.set_timeout(0ms, [&] { fired.push_back(2); }, "chained");
        },
        "scheduling");

    loop.poll();
    // Timers scheduled from a callback must not fire within the same poll.
    REQUIRE(fired == std::vector<int>{1});

    std::this_thread::sleep_for(2ms);
    loop.poll();
    REQUIRE(fired == std::vector<int>{1, 2});
}

TEST_CASE("Cancelling a pending timer from another timer's callback is honored", "[event_loop]") {
    nk::EventLoop loop;
    int canceller_ran = 0;
    int victim_ran = 0;
    nk::CallbackHandle victim{};

    (void)loop.set_timeout(
        0ms,
        [&] {
            ++canceller_ran;
            loop.cancel(victim);
        },
        "canceller");
    victim = loop.set_timeout(0ms, [&] { ++victim_ran; }, "victim");

    loop.poll();
    loop.poll();
    REQUIRE(canceller_ran == 1);
    REQUIRE(victim_ran == 0);
}

TEST_CASE("Cancelling the running interval from inside its callback stops re-arming",
          "[event_loop]") {
    nk::EventLoop loop;
    int ticks = 0;
    nk::CallbackHandle self{};

    self = loop.set_interval(
        1ms,
        [&] {
            ++ticks;
            loop.cancel(self);
        },
        "self-cancelling");
    loop.poll();
    std::this_thread::sleep_for(2ms);
    loop.poll();
    REQUIRE(ticks == 1);
}

TEST_CASE("Nested poll from a timer callback runs posted tasks without refiring the timer",
          "[event_loop]") {
    nk::EventLoop loop;
    int timer_ran = 0;
    int posted_ran = 0;

    (void)loop.set_timeout(
        0ms,
        [&] {
            ++timer_ran;
            loop.post([&] { ++posted_ran; }, "nested-post");
            loop.poll();
        },
        "nesting");

    loop.poll();
    REQUIRE(timer_ran == 1);
    REQUIRE(posted_ran == 1);
}

TEST_CASE("Posted tasks run before timers within a poll", "[event_loop]") {
    nk::EventLoop loop;
    std::vector<int> order;

    (void)loop.set_timeout(0ms, [&] { order.push_back(2); }, "timer");
    loop.post([&] { order.push_back(1); }, "task");
    loop.poll();

    REQUIRE(order == std::vector<int>{1, 2});
}
