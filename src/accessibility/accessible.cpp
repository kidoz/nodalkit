#include <algorithm>
#include <nk/accessibility/accessible.h>

namespace nk {

std::string_view accessible_action_name(AccessibleAction action) noexcept {
    switch (action) {
    case AccessibleAction::Activate:
        return "activate";
    case AccessibleAction::Focus:
        return "focus";
    case AccessibleAction::Toggle:
        return "toggle";
    }
    return "activate";
}

std::string_view accessible_relation_kind_name(AccessibleRelationKind kind) noexcept {
    switch (kind) {
    case AccessibleRelationKind::LabelledBy:
        return "labelled-by";
    case AccessibleRelationKind::DescribedBy:
        return "described-by";
    case AccessibleRelationKind::Controls:
        return "controls";
    }
    return "labelled-by";
}

struct Accessible::Impl {
    struct ActionBinding {
        AccessibleAction action = AccessibleAction::Activate;
        std::function<bool()> handler;
    };

    AccessibleRole role = AccessibleRole::None;
    std::string name;
    std::string description;
    bool hidden = false;
    StateFlags state = StateFlags::None;
    std::string value;
    std::vector<AccessibleAction> action_list;
    std::vector<ActionBinding> actions;
    std::vector<AccessibleRelation> relations;
};

Accessible::Accessible() : impl_(std::make_unique<Impl>()) {}

Accessible::~Accessible() = default;

AccessibleRole Accessible::role() const {
    return impl_->role;
}

void Accessible::set_role(AccessibleRole role) {
    impl_->role = role;
}

std::string_view Accessible::name() const {
    return impl_->name;
}

void Accessible::set_name(std::string name) {
    impl_->name = std::move(name);
}

std::string_view Accessible::description() const {
    return impl_->description;
}

void Accessible::set_description(std::string description) {
    impl_->description = std::move(description);
}

bool Accessible::is_hidden() const {
    return impl_->hidden;
}

void Accessible::set_hidden(bool hidden) {
    impl_->hidden = hidden;
}

StateFlags Accessible::state() const {
    return impl_->state;
}

void Accessible::set_state(StateFlags state) {
    impl_->state = state;
}

std::string_view Accessible::value() const {
    return impl_->value;
}

void Accessible::set_value(std::string value) {
    impl_->value = std::move(value);
}

void Accessible::add_action(AccessibleAction action, std::function<bool()> handler) {
    for (auto& binding : impl_->actions) {
        if (binding.action == action) {
            binding.handler = std::move(handler);
            return;
        }
    }
    impl_->action_list.push_back(action);
    impl_->actions.push_back({action, std::move(handler)});
}

void Accessible::remove_action(AccessibleAction action) {
    impl_->action_list.erase(
        std::remove(impl_->action_list.begin(), impl_->action_list.end(), action),
        impl_->action_list.end());
    impl_->actions.erase(std::remove_if(impl_->actions.begin(),
                                        impl_->actions.end(),
                                        [&](const Impl::ActionBinding& binding) {
                                            return binding.action == action;
                                        }),
                         impl_->actions.end());
}

bool Accessible::supports_action(AccessibleAction action) const {
    return std::any_of(
        impl_->actions.begin(), impl_->actions.end(), [&](const Impl::ActionBinding& binding) {
            return binding.action == action;
        });
}

std::span<const AccessibleAction> Accessible::actions() const {
    return impl_->action_list;
}

bool Accessible::perform_action(AccessibleAction action) const {
    const auto it =
        std::find_if(impl_->actions.begin(),
                     impl_->actions.end(),
                     [&](const Impl::ActionBinding& binding) { return binding.action == action; });
    if (it == impl_->actions.end()) {
        return false;
    }
    return it->handler ? it->handler() : true;
}

void Accessible::set_relation(AccessibleRelationKind kind, std::string target_debug_name) {
    for (auto& relation : impl_->relations) {
        if (relation.kind == kind) {
            relation.target_debug_name = std::move(target_debug_name);
            return;
        }
    }
    impl_->relations.push_back({kind, std::move(target_debug_name)});
}

void Accessible::remove_relation(AccessibleRelationKind kind) {
    impl_->relations.erase(
        std::remove_if(impl_->relations.begin(),
                       impl_->relations.end(),
                       [&](const AccessibleRelation& relation) { return relation.kind == kind; }),
        impl_->relations.end());
}

std::span<const AccessibleRelation> Accessible::relations() const {
    return impl_->relations;
}

} // namespace nk
