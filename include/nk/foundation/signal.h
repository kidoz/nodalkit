#include <type_traits>
#pragma once

/// @file signal.h
/// @brief Type-safe signal/slot system without code generation.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <nk/runtime/event_loop.h>

namespace nk {

namespace detail {

/// Shared state between a Signal slot and the Connection handle.
struct ConnectionState {
    std::atomic<bool> connected{true};
    uint64_t id{0};
};

} // namespace detail

/// Handle to a signal connection. Can be used to disconnect later.
/// Lightweight, copyable, and safe to hold after the signal is destroyed.
class Connection {
public:
    Connection() = default;

    /// Disconnect this slot from its signal. Idempotent.
    void disconnect();

    /// Returns true if the connection is still active.
    [[nodiscard]] bool connected() const;

private:
    template <typename...> friend class Signal;

    explicit Connection(std::shared_ptr<detail::ConnectionState> state);
    std::shared_ptr<detail::ConnectionState> state_;
};

/// RAII wrapper that disconnects on destruction.
class ScopedConnection {
public:
    ScopedConnection() = default;
    explicit ScopedConnection(Connection conn);
    ~ScopedConnection();

    ScopedConnection(ScopedConnection&& other) noexcept;
    ScopedConnection& operator=(ScopedConnection&& other) noexcept;

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    void disconnect();
    [[nodiscard]] Connection release();
    [[nodiscard]] bool connected() const;

private:
    Connection conn_;
};

/// Type-safe signal. Slots are std::function<void(Args...)>.
///
/// Signals are non-copyable but movable. Emitting during emission is
/// supported (reentrant-safe). Disconnecting a slot during emission is safe.
///
/// Usage:
/// @code
///   nk::Signal<int> value_changed;
///   auto conn = value_changed.connect([](int v) { /* ... */ });
///   value_changed.emit(42);
///   conn.disconnect();
/// @endcode
template <typename... Args> class Signal {
public:
    using SlotType = std::function<void(const Args&...)>;

    Signal() = default;
    ~Signal() = default;

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    Signal(Signal&&) noexcept = default;
    Signal& operator=(Signal&&) noexcept = default;

    /// Connect a slot. Returns a Connection handle.
    [[nodiscard]] Connection connect(SlotType slot) {
        auto state = std::make_shared<detail::ConnectionState>();
        std::lock_guard lock(shared_->mutex);
        state->id = shared_->next_id++;
        shared_->slots.push_back({
            state,
            std::move(slot),
            EventLoop::current(),
            std::this_thread::get_id()
        });
        return Connection(std::move(state));
    }

    /// Emit the signal, invoking all connected slots.
    void emit(const Args&... args) {
        std::vector<Slot> local_slots;
        {
            std::lock_guard lock(shared_->mutex);
            purge_disconnected();
            local_slots = shared_->slots;
        }

        const auto current_thread = std::this_thread::get_id();
        for (const auto& s : local_slots) {
            if (s.state->connected.load(std::memory_order_relaxed)) {
                if (s.target_thread == current_thread || !s.target_loop) {
                    s.callback(args...);
                } else {
                    if constexpr (std::conjunction_v<std::is_copy_constructible<std::decay_t<Args>>...>) {
                        auto cb = s.callback;
                        s.target_loop->post([cb, args...]() mutable {
                            cb(args...);
                        }, "signal-cross-thread");
                    } else {
                        // Fallback to direct invocation if arguments are not copyable,
                        // even though it is not thread-safe.
                        s.callback(args...);
                    }
                }
            }
        }
    }

    /// Convenience: emit via operator().
    void operator()(const Args&... args) { emit(args...); }

    /// Number of currently connected slots.
    [[nodiscard]] std::size_t connection_count() const {
        std::lock_guard lock(shared_->mutex);
        std::size_t count = 0;
        for (const auto& s : shared_->slots) {
            if (s.state->connected.load(std::memory_order_relaxed)) {
                ++count;
            }
        }
        return count;
    }

    /// Disconnect all slots.
    void disconnect_all() {
        std::lock_guard lock(shared_->mutex);
        for (auto& s : shared_->slots) {
            s.state->connected.store(false, std::memory_order_relaxed);
        }
        shared_->slots.clear();
    }

private:
    struct Slot {
        std::shared_ptr<detail::ConnectionState> state;
        SlotType callback;
        EventLoop* target_loop;
        std::thread::id target_thread;
    };

    struct SharedState {
        std::mutex mutex;
        std::vector<Slot> slots;
        uint64_t next_id = 1;
    };

    std::unique_ptr<SharedState> shared_{std::make_unique<SharedState>()};

    void purge_disconnected() {
        std::erase_if(shared_->slots, [](const Slot& s) {
            return !s.state->connected.load(std::memory_order_relaxed);
        });
    }
};

} // namespace nk
