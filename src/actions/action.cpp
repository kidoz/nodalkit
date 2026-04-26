#include <nk/actions/action.h>
#include <unordered_map>

namespace nk {

// --- Action ---

struct Action::Impl {
    std::string name;
    bool enabled = true;
    Signal<> on_activated;
};

Action::Action(std::string name) : impl_(std::make_unique<Impl>()) {
    impl_->name = std::move(name);
}

Action::~Action() = default;

std::string_view Action::name() const {
    return impl_->name;
}

bool Action::is_enabled() const {
    return impl_->enabled;
}

void Action::set_enabled(bool enabled) {
    impl_->enabled = enabled;
}

void Action::activate() {
    if (impl_->enabled) {
        impl_->on_activated.emit();
    }
}

Signal<>& Action::on_activated() {
    return impl_->on_activated;
}

// --- ActionGroup ---

struct ActionGroup::Impl {
    std::unordered_map<std::string, std::shared_ptr<Action>> actions;
};

ActionGroup::ActionGroup() : impl_(std::make_unique<Impl>()) {}

ActionGroup::~ActionGroup() = default;

void ActionGroup::add(std::shared_ptr<Action> action) {
    auto name = std::string(action->name());
    impl_->actions[name] = std::move(action);
}

Action* ActionGroup::lookup(std::string_view name) const {
    auto it = impl_->actions.find(std::string(name));
    if (it != impl_->actions.end()) {
        return it->second.get();
    }
    return nullptr;
}

void ActionGroup::activate(std::string_view name) {
    if (auto* action = lookup(name)) {
        action->activate();
    }
}

} // namespace nk
