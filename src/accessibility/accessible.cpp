#include <nk/accessibility/accessible.h>

namespace nk {

struct Accessible::Impl {
    AccessibleRole role = AccessibleRole::None;
    std::string name;
    std::string description;
    bool hidden = false;
    StateFlags state = StateFlags::None;
};

Accessible::Accessible() : impl_(std::make_unique<Impl>()) {}
Accessible::~Accessible() = default;

AccessibleRole Accessible::role() const { return impl_->role; }
void Accessible::set_role(AccessibleRole role) { impl_->role = role; }

std::string_view Accessible::name() const { return impl_->name; }
void Accessible::set_name(std::string name) {
    impl_->name = std::move(name);
}

std::string_view Accessible::description() const {
    return impl_->description;
}
void Accessible::set_description(std::string description) {
    impl_->description = std::move(description);
}

bool Accessible::is_hidden() const { return impl_->hidden; }
void Accessible::set_hidden(bool hidden) { impl_->hidden = hidden; }

StateFlags Accessible::state() const { return impl_->state; }
void Accessible::set_state(StateFlags state) { impl_->state = state; }

} // namespace nk
