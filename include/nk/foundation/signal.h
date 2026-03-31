#pragma once

/// @file signal.h
/// @brief Type-safe signal/slot system without code generation.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

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
    template <typename...>
    friend class Signal;

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

    ScopedConnection(ScopedConnection const&) = delete;
    ScopedConnection& operator=(ScopedConnection const&) = delete;

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
template <typename... Args>
class Signal {
public:
    using SlotType = std::function<void(Args const&...)>;

    Signal() = default;
    ~Signal() = default;

    Signal(Signal const&) = delete;
    Signal& operator=(Signal const&) = delete;

    Signal(Signal&&) noexcept = default;
    Signal& operator=(Signal&&) noexcept = default;

    /// Connect a slot. Returns a Connection handle.
    [[nodiscard]] Connection connect(SlotType slot) {
        auto state = std::make_shared<detail::ConnectionState>();
        state->id = next_id_++;
        slots_.push_back({state, std::move(slot)});
        return Connection(std::move(state));
    }

    /// Emit the signal, invoking all connected slots.
    void emit(Args const&... args) {
        // Take a snapshot size so newly connected slots during emission
        // are not called in this round.
        auto const n = slots_.size();
        ++emit_depth_;
        for (std::size_t i = 0; i < n; ++i) {
            if (slots_[i].state->connected.load(std::memory_order_relaxed)) {
                slots_[i].callback(args...);
            }
        }
        --emit_depth_;

        if (emit_depth_ == 0) {
            purge_disconnected();
        }
    }

    /// Convenience: emit via operator().
    void operator()(Args const&... args) { emit(args...); }

    /// Number of currently connected slots.
    [[nodiscard]] std::size_t connection_count() const {
        std::size_t count = 0;
        for (auto const& s : slots_) {
            if (s.state->connected.load(std::memory_order_relaxed)) {
                ++count;
            }
        }
        return count;
    }

    /// Disconnect all slots.
    void disconnect_all() {
        for (auto& s : slots_) {
            s.state->connected.store(false, std::memory_order_relaxed);
        }
        if (emit_depth_ == 0) {
            slots_.clear();
        }
    }

private:
    struct Slot {
        std::shared_ptr<detail::ConnectionState> state;
        SlotType callback;
    };

    void purge_disconnected() {
        std::erase_if(slots_, [](Slot const& s) {
            return !s.state->connected.load(std::memory_order_relaxed);
        });
    }

    std::vector<Slot> slots_;
    int emit_depth_ = 0;
    uint64_t next_id_ = 1;
};

} // namespace nk
