#include "src/platform/linux/clipboard_transfer.h"

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <fcntl.h>
#include <nk/runtime/event_loop.h>
#include <unistd.h>
#include <utility>

namespace {
struct Pipe {
    std::array<int, 2> ends{-1, -1};

    Pipe() { REQUIRE(pipe2(ends.data(), O_CLOEXEC) == 0); }

    ~Pipe() {
        for (int fd : ends) {
            if (fd >= 0) {
                close(fd);
            }
        }
    }

    int take(std::size_t end) { return std::exchange(ends[end], -1); }
};

using Transfer = nk::detail::ClipboardTransfer;
} // namespace

TEST_CASE("Clipboard receives complete text and rejects oversized streams",
          "[wayland][clipboard]") {
    Pipe pipe;
    REQUIRE(write(pipe.ends[1], "hello", 5) == 5);
    close(pipe.take(1));
    SECTION("EOF completes the transfer") {
        Transfer transfer(pipe.take(0), Transfer::Direction::Read);
        REQUIRE(transfer.poll());
        REQUIRE(transfer.succeeded());
        CHECK(transfer.take_text() == "hello");
    }
    SECTION("Oversized input fails without returning a partial paste") {
        Transfer transfer(pipe.take(0), Transfer::Direction::Read, {}, 4);
        REQUIRE(transfer.poll());
        CHECK_FALSE(transfer.succeeded());
        CHECK(transfer.take_text().empty());
    }
}

TEST_CASE("A stalled clipboard source does not prevent UI tasks from running",
          "[wayland][clipboard]") {
    Pipe pipe;
    Transfer transfer(pipe.take(0), Transfer::Direction::Read);
    nk::EventLoop loop;
    bool ran = false;
    loop.post([&] { ran = true; });
    REQUIRE_FALSE(transfer.poll()); // Writer intentionally keeps the pipe open.
    loop.poll();
    CHECK(ran);
}

TEST_CASE("Clipboard deadlines and cancellation close their pipe", "[wayland][clipboard]") {
    Pipe pipe;
    const int fd = pipe.take(0);
    SECTION("Deadline") {
        Transfer transfer(
            fd, Transfer::Direction::Read, {}, Transfer::MaxBytes, std::chrono::milliseconds(0));
        REQUIRE(transfer.poll());
        CHECK_FALSE(transfer.succeeded());
    }
    SECTION("Cancellation") {
        Transfer transfer(fd, Transfer::Direction::Read);
        REQUIRE_FALSE(transfer.poll());
    }
    CHECK(fcntl(fd, F_GETFL) == -1);
    CHECK(errno == EBADF);
}

TEST_CASE("Clipboard sends yield to stalled readers", "[wayland][clipboard]") {
    Pipe pipe;
    Transfer transfer(pipe.take(1), Transfer::Direction::Write, std::string(1024 * 1024, 'x'));
    CHECK_FALSE(transfer.poll());
    CHECK_FALSE(transfer.poll());
    CHECK_FALSE(transfer.succeeded());
}

TEST_CASE("A closed clipboard reader cannot terminate the application with SIGPIPE",
          "[wayland][clipboard]") {
    Pipe pipe;
    close(pipe.take(0));
    Transfer transfer(pipe.take(1), Transfer::Direction::Write, "text");
    REQUIRE(transfer.poll());
    CHECK_FALSE(transfer.succeeded());
}
