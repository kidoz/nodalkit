#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace nk::detail {

/// A bounded, nonblocking clipboard pipe. Owns and closes fd, including on failure.
class ClipboardTransfer {
public:
    static constexpr std::size_t MaxBytes = 16U * 1024U * 1024U;
    enum class Direction : uint8_t { Read, Write };
    ClipboardTransfer(int fd,
                      Direction direction,
                      std::string text = {},
                      std::size_t max_bytes = MaxBytes,
                      std::chrono::milliseconds timeout = std::chrono::seconds(2));
    ~ClipboardTransfer();
    ClipboardTransfer(const ClipboardTransfer&) = delete;
    ClipboardTransfer& operator=(const ClipboardTransfer&) = delete;
    ClipboardTransfer(ClipboardTransfer&&) = delete;
    ClipboardTransfer& operator=(ClipboardTransfer&&) = delete;

    /// Do at most 64 KiB of work. Returns true when finished (success or failure).
    bool poll();

    [[nodiscard]] bool succeeded() const { return succeeded_; }

    [[nodiscard]] int fd() const { return fd_; }

    [[nodiscard]] std::string take_text();

private:
    void finish(bool success);
    int fd_;
    Direction direction_;
    std::string text_;
    std::size_t max_bytes_;
    std::size_t offset_ = 0;
    std::chrono::steady_clock::time_point deadline_;
    bool finished_ = false;
    bool succeeded_ = false;
};

} // namespace nk::detail
