#include <nk/actions/action.h>
#include <stdexcept>
#include <unordered_map>

namespace nk {

// --- Action ---

struct Action::Impl {
    std::string name;
    bool enabled = true;
    bool checkable = false;
    bool checked = false;
    Signal<> changed;
    Signal<> on_activated;
};

Action::Action(std::string name) : impl_(std::make_shared<Impl>()) {
    impl_->name = std::move(name);
}

Action::~Action() {
    impl_->changed.disconnect_all();
    impl_->on_activated.disconnect_all();
}

std::string_view Action::name() const {
    return impl_->name;
}

bool Action::is_enabled() const {
    return impl_->enabled;
}

void Action::set_enabled(bool enabled) {
    const auto state = impl_;
    if (state->enabled != enabled) {
        state->enabled = enabled;
        state->changed.emit();
    }
}

bool Action::is_checkable() const {
    return impl_->checkable;
}

void Action::set_checkable(bool checkable) {
    const auto state = impl_;
    if (state->checkable != checkable) {
        state->checkable = checkable;
        if (!checkable) {
            state->checked = false;
        }
        state->changed.emit();
    }
}

bool Action::is_checked() const {
    return impl_->checked;
}

void Action::set_checked(bool checked) {
    const auto state = impl_;
    if (state->checkable && state->checked != checked) {
        state->checked = checked;
        state->changed.emit();
    }
}

Signal<>& Action::on_changed() {
    return impl_->changed;
}

void Action::activate() {
    const auto state = impl_;
    if (state->enabled) {
        if (state->checkable) {
            state->checked = !state->checked;
            state->changed.emit();
        }
        state->on_activated.emit();
    }
}

Signal<>& Action::on_activated() {
    return impl_->on_activated;
}

// --- ActionGroup ---

struct ActionGroup::Impl {
    struct Entry {
        std::shared_ptr<Action> action;
        ScopedConnection changed;
    };

    std::unordered_map<std::string, Entry> actions;
    Signal<std::string_view> changed;
};

ActionGroup::ActionGroup() : impl_(std::make_unique<Impl>()) {}

ActionGroup::~ActionGroup() = default;

void ActionGroup::add(std::shared_ptr<Action> action) {
    if (!action) {
        throw std::invalid_argument("ActionGroup requires a non-null action");
    }
    auto name = std::string(action->name());
    auto connection = action->on_changed().connect([this, name] { impl_->changed.emit(name); });
    impl_->actions.insert_or_assign(
        name, Impl::Entry{std::move(action), ScopedConnection{std::move(connection)}});
    impl_->changed.emit(name);
}

void ActionGroup::remove(std::string_view name) {
    const std::string owned_name{name};
    if (impl_->actions.erase(owned_name) != 0) {
        impl_->changed.emit(owned_name);
    }
}

std::shared_ptr<Action> ActionGroup::find(std::string_view name) const {
    const auto it = impl_->actions.find(std::string(name));
    return it == impl_->actions.end() ? nullptr : it->second.action;
}

Signal<std::string_view>& ActionGroup::on_changed() {
    return impl_->changed;
}

Action* ActionGroup::lookup(std::string_view name) const {
    return find(name).get();
}

void ActionGroup::activate(std::string_view name) {
    if (auto action = find(name)) {
        action->activate();
    }
}

} // namespace nk
