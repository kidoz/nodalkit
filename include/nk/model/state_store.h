#pragma once

/// @file state_store.h
/// @brief Formal state container for Unidirectional Data Flow (MVI/MVVM).

#include <nk/foundation/property.h>
#include <mutex>

namespace nk {

/// A State Container to facilitate Unidirectional Data Flow.
///
/// UI views should read from or bind to `state()` and never mutate it directly.
/// Instead, views call `dispatch()` with an Intent (action/event).
/// The concrete store processes the intent and updates the state.
///
/// @tparam State A value type representing the UI state.
/// @tparam Intent An enum or variant representing actions that can occur.
template <typename State, typename Intent>
class StateStore {
public:
    explicit StateStore(State initial_state)
        : state_(std::move(initial_state)) {}

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
    /// Should only be called within dispatch() or async callbacks triggered by dispatch().
    void update_state(State new_state) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_.set(std::move(new_state));
    }

    /// Access the current state value securely for mutation logic.
    [[nodiscard]] State current_state() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_.get();
    }

private:
    Property<State> state_;
    mutable std::mutex mutex_;
};

} // namespace nk