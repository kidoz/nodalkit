#pragma once

/// @file action.h
/// @brief Decoupled named actions for menus, shortcuts, and commands.

#include <memory>
#include <nk/foundation/signal.h>
#include <string>
#include <string_view>

namespace nk {

/// A named, activatable action. Actions decouple command logic from
/// the concrete widget that triggers them. They can be bound to
/// menu items, shortcuts, or buttons.
class Action {
public:
    /// Create a named action.
    explicit Action(std::string name);
    ~Action();

    Action(const Action&) = delete;
    Action& operator=(const Action&) = delete;

    /// The action name (e.g. "app.quit", "win.save").
    [[nodiscard]] std::string_view name() const;

    /// Whether the action is currently enabled.
    [[nodiscard]] bool is_enabled() const;
    void set_enabled(bool enabled);

    /// Activate the action.
    void activate();

    /// Emitted when the action is activated.
    Signal<>& on_activated();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Groups related actions together (e.g. all window-scoped actions).
class ActionGroup {
public:
    ActionGroup();
    ~ActionGroup();

    ActionGroup(const ActionGroup&) = delete;
    ActionGroup& operator=(const ActionGroup&) = delete;

    /// Add an action to this group. The group takes shared ownership.
    void add(std::shared_ptr<Action> action);

    /// Look up an action by name.
    [[nodiscard]] Action* lookup(std::string_view name) const;

    /// Activate an action by name. No-op if not found or disabled.
    void activate(std::string_view name);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
