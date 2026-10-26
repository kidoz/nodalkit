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
#include <nk/runtime/event_loop.h>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

namespace nk {

namespace detail {

/// Shared state between a Signal slot and the Connection handle.
struct ConnectionState {
    std::atomic<bool> connected{true};
    uint64_t id{0};
};

// Queued views need backing storage: copying the view alone retains a borrowed
// pointer into the emitter's memory. Other value types must themselves own any
// data needed by a receiver after emit() returns.
template <typename T> struct QueuedSignalArgument {
    static constexpr bool supported = std::is_copy_constructible_v<T> && !std::is_pointer_v<T>;
    T value;

    explicit QueuedSignalArgument(const T& arg) : value(arg) {}

    const T& get() const { return value; }
};

template <typename Char, typename Traits>
struct QueuedSignalArgument<std::basic_string_view<Char, Traits>> {
    static constexpr bool supported = true;
    std::basic_string<Char, Traits> value;

    explicit QueuedSignalArgument(std::basic_string_view<Char, Traits> arg) : value(arg) {}

    std::basic_string_view<Char, Traits> get() const { return value; }
};

template <typename T> struct QueuedSignalArgument<std::reference_wrapper<T>> {
    static constexpr bool supported = false;
};

template <typename T, std::size_t Extent> struct QueuedSignalArgument<std::span<T, Extent>> {
    static constexpr bool supported =
        std::is_const_v<T> && std::is_copy_constructible_v<std::remove_const_t<T>>;
    std::vector<std::remove_const_t<T>> value;

    explicit QueuedSignalArgument(std::span<T, Extent> arg) : value(arg.begin(), arg.end()) {}

    std::span<T, Extent> get() const { return std::span<T, Extent>(value); }
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
/// Disconnection or signal destruction also cancels queued deliveries that have
/// not started. Cross-thread delivery copies string_view text and const span
/// elements; other values and span elements must own their referenced data.
/// Mutable references, reference wrappers, raw pointers,
/// and non-copyable payloads cannot be queued and cause std::logic_error.
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

    ~Signal() { disconnect_all(); }

    Signal(const Signal&) = delete;
    Signal& operator=(const Signal&) = delete;

    Signal(Signal&&) noexcept = default;

    Signal& operator=(Signal&& other) noexcept {
        if (this != &other) {
            disconnect_all();
            shared_ = std::move(other.shared_);
        }
        return *this;
    }

    /// Connect a slot. Returns a Connection handle.
    [[nodiscard]] Connection connect(SlotType slot) {
        auto state = std::make_shared<detail::ConnectionState>();
        std::lock_guard lock(shared_->mutex);
        state->id = shared_->next_id++;
        auto* loop = EventLoop::current();
        shared_->slots.push_back(
            {state,
             std::move(slot),
             loop ? loop->task_poster("signal-cross-thread") : EventLoop::TaskPoster{},
             std::this_thread::get_id()});
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
                if (s.target_thread == current_thread || !s.target_poster) {
                    s.callback(args...);
                } else {
                    if constexpr (((detail::QueuedSignalArgument<
                                        std::remove_cvref_t<Args>>::supported &&
                                    (!std::is_lvalue_reference_v<Args> ||
                                     std::is_const_v<std::remove_reference_t<Args>>)) &&
                                   ...)) {
                        s.target_poster(
                            [state = s.state,
                             cb = s.callback,
                             values =
                                 std::tuple{detail::QueuedSignalArgument<std::remove_cvref_t<Args>>(
                                     args)...}] {
                                if (state->connected.load(std::memory_order_relaxed)) {
                                    std::apply(
                                        [&cb](const auto&... stored) { cb(stored.get()...); },
                                        values);
                                }
                            });
                    } else {
                        throw std::logic_error("Signal payload cannot be delivered across threads");
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
        if (!shared_) {
            return;
        }
        std::vector<Slot> disconnected;
        {
            std::lock_guard lock(shared_->mutex);
            for (auto& s : shared_->slots) {
                s.state->connected.store(false, std::memory_order_relaxed);
            }
            disconnected.swap(shared_->slots);
        }
    }

private:
    struct Slot {
        std::shared_ptr<detail::ConnectionState> state;
        SlotType callback;
        EventLoop::TaskPoster target_poster;
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
