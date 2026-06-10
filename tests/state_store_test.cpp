/// @file state_store_test.cpp
/// @brief Tests for the unidirectional StateStore.

#include <catch2/catch_test_macros.hpp>
#include <nk/model/state_store.h>

struct AppState {
    int counter = 0;
    std::string message;

    bool operator==(const AppState& other) const {
        return counter == other.counter && message == other.message;
    }
};

enum class AppIntent { Increment, Decrement, SetMessage };

class CounterStore : public nk::StateStore<AppState, AppIntent> {
public:
    CounterStore() : nk::StateStore<AppState, AppIntent>({0, "init"}) {}

    void dispatch(AppIntent intent) override {
        AppState s = current_state();
        switch (intent) {
        case AppIntent::Increment:
            s.counter++;
            break;
        case AppIntent::Decrement:
            s.counter--;
            break;
        case AppIntent::SetMessage:
            s.message = "updated";
            break;
        }
        update_state(s);
    }
};

TEST_CASE("StateStore exposes initial state", "[state_store]") {
    CounterStore store;
    REQUIRE(store.state().get().counter == 0);
    REQUIRE(store.state().get().message == "init");
}

TEST_CASE("StateStore processes intents and updates state", "[state_store]") {
    CounterStore store;

    int emit_count = 0;
    auto conn = store.state().on_changed().connect([&](const AppState&) { emit_count++; });

    store.dispatch(AppIntent::Increment);
    REQUIRE(store.state().get().counter == 1);
    REQUIRE(emit_count == 1);

    store.dispatch(AppIntent::Decrement);
    REQUIRE(store.state().get().counter == 0);
    REQUIRE(emit_count == 2);

    store.dispatch(AppIntent::SetMessage);
    REQUIRE(store.state().get().message == "updated");
    REQUIRE(emit_count == 3);
}

TEST_CASE("StateStore supports re-entrant dispatch from an observer", "[state_store]") {
    CounterStore store;

    // An observer that reacts to the first update by dispatching again.
    // Without the lock-free update path this deadlocked: update_state() held
    // the mutex while emitting, and the nested dispatch re-locked it.
    auto conn = store.state().on_changed().connect([&](const AppState& s) {
        if (s.counter == 1) {
            store.dispatch(AppIntent::Increment);
        }
    });

    store.dispatch(AppIntent::Increment);
    REQUIRE(store.state().get().counter == 2);
}
