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
/// menu items, shortcuts, or buttons. Mutate actions and groups on the UI thread.
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

    /// Checkable actions toggle their checked state before activation is emitted.
    [[nodiscard]] bool is_checkable() const;
    void set_checkable(bool checkable);
    [[nodiscard]] bool is_checked() const;
    /// Changes checked state only when checkable; does not activate the action.
    void set_checked(bool checked);

    /// Emitted once when enabled, checkable, or checked state changes.
    Signal<>& on_changed();

    /// Activate the action.
    void activate();

    /// Emitted when the action is activated.
    Signal<>& on_activated();

private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

/// Groups related actions together (e.g. all window-scoped actions).
class ActionGroup {
public:
    ActionGroup();
    ~ActionGroup();

    ActionGroup(const ActionGroup&) = delete;
    ActionGroup& operator=(const ActionGroup&) = delete;

    /// Add or replace an action by name. Null actions throw std::invalid_argument.
    void add(std::shared_ptr<Action> action);

    /// Remove an action; existing owners may retain it independently.
    void remove(std::string_view name);

    /// Look up an action with shared ownership, safe across registry mutations.
    [[nodiscard]] std::shared_ptr<Action> find(std::string_view name) const;

    /// Emitted with the action name after addition, removal, or state change.
    Signal<std::string_view>& on_changed();

    /// Look up an action by name.
    [[nodiscard]] Action* lookup(std::string_view name) const;

    /// Activate an action by name. No-op if not found or disabled.
    void activate(std::string_view name);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
