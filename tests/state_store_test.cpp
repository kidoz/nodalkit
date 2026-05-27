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

enum class AppIntent {
    Increment,
    Decrement,
    SetMessage
};

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
    auto conn = store.state().on_changed().connect([&](const AppState&) {
        emit_count++;
    });

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