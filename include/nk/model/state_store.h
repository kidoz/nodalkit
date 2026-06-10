#pragma once

/// @file state_store.h
/// @brief Formal state container for Unidirectional Data Flow (MVI/MVVM).

#include <nk/foundation/property.h>
#include <utility>

namespace nk {

/// A State Container to facilitate Unidirectional Data Flow.
///
/// UI views should read from or bind to `state()` and never mutate it directly.
/// Instead, views call `dispatch()` with an Intent (action/event).
/// The concrete store processes the intent and updates the state.
///
/// StateStore is not thread-safe. Construct it, dispatch intents, and observe
/// state() on the thread that owns the store (normally the UI thread). Async
/// work must post its result back to that thread before touching the store,
/// e.g. via EventLoop::current()->post().
///
/// Re-entrant dispatch is supported: an on_changed() observer may dispatch
/// another intent, and the nested update completes before control returns.
///
/// @tparam State A value type representing the UI state.
/// @tparam Intent An enum or variant representing actions that can occur.
template <typename State, typename Intent> class StateStore {
public:
    explicit StateStore(State initial_state) : state_(std::move(initial_state)) {}

    virtual ~StateStore() = default;

    StateStore(const StateStore&) = delete;
    StateStore& operator=(const StateStore&) = delete;

    /// The observable state. Views bind to this or listen to its on_changed() signal.
    [[nodiscard]] const Property<State>& state() const { return state_; }

    /// The observable state (mutable reference to bind or listen, but shouldn't be
    /// directly `set()` by views in a strict unidirectional flow).
    [[nodiscard]] Property<State>& state() { return state_; }

    /// Dispatch an intent (action/event) to update the state.
    /// Implementations must override this to define how intents mutate the state.
    virtual void dispatch(Intent intent) = 0;

protected:
    /// Protected helper to mutate state and notify observers.
    /// Should only be called within dispatch() or callbacks that have been
    /// posted back to the owning thread.
    void update_state(State new_state) { state_.set(std::move(new_state)); }

    /// Returns a copy of the current state value for mutation logic.
    [[nodiscard]] State current_state() const { return state_.get(); }

private:
    Property<State> state_;
};

} // namespace nk
