#pragma once

#include <functional>
#include <memory>
#include <nk/ui_core/widget.h>
#include <string>
#include <utility>

namespace nk::detail {

// A semantic child for a command painted by its owning composite control.
// It shares the widget identity/lifetime contract used by every native bridge.
// Pointer input continues to bubble through the painted owner's hit test.
class AccessibleItem final : public Widget {
public:
    using Widget::set_state_flag;

    void append(std::shared_ptr<Widget> child) { append_child(std::move(child)); }

    static std::shared_ptr<AccessibleItem>
    create(std::string name, AccessibleRole role, std::function<bool()> activate) {
        auto item = std::shared_ptr<AccessibleItem>(new AccessibleItem());
        auto& accessible = item->ensure_accessible();
        accessible.set_role(role);
        accessible.set_name(std::move(name));
        accessible.add_action(AccessibleAction::Activate, std::move(activate));
        return item;
    }

    [[nodiscard]] bool hit_test(Point) const override { return false; }

protected:
    void snapshot(SnapshotContext&) const override {}
};

} // namespace nk::detail
