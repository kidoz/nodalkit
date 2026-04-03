#pragma once

/// @file accessible.h
/// @brief Accessibility metadata attached to widgets.

#include <functional>
#include <memory>
#include <nk/accessibility/role.h>
#include <nk/ui_core/state_flags.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

enum class AccessibleAction : uint8_t {
    Activate,
    Focus,
    Toggle,
};

[[nodiscard]] std::string_view accessible_action_name(AccessibleAction action) noexcept;

enum class AccessibleRelationKind : uint8_t {
    LabelledBy,
    DescribedBy,
    Controls,
};

[[nodiscard]] std::string_view accessible_relation_kind_name(AccessibleRelationKind kind) noexcept;

struct AccessibleRelation {
    AccessibleRelationKind kind = AccessibleRelationKind::LabelledBy;
    std::string target_debug_name;

    bool operator==(const AccessibleRelation&) const = default;
};

/// Provides accessibility information for a widget.
/// Each widget can have an Accessible that exposes its role,
/// name, description, and live state to assistive technologies.
class Accessible {
public:
    Accessible();
    ~Accessible();

    Accessible(const Accessible&) = delete;
    Accessible& operator=(const Accessible&) = delete;

    /// The semantic role (Button, TextInput, etc.).
    [[nodiscard]] AccessibleRole role() const;
    void set_role(AccessibleRole role);

    /// Human-readable name (e.g. button label text).
    [[nodiscard]] std::string_view name() const;
    void set_name(std::string name);

    /// Extended description for screen readers.
    [[nodiscard]] std::string_view description() const;
    void set_description(std::string description);

    /// Whether the element is marked as hidden from AT.
    [[nodiscard]] bool is_hidden() const;
    void set_hidden(bool hidden);

    /// Current interactive state flags relevant to AT.
    [[nodiscard]] StateFlags state() const;
    void set_state(StateFlags state);

    /// Optional user-visible value for editable/value-bearing widgets.
    [[nodiscard]] std::string_view value() const;
    void set_value(std::string value);

    /// Supported accessible actions.
    void add_action(AccessibleAction action, std::function<bool()> handler = {});
    void remove_action(AccessibleAction action);
    [[nodiscard]] bool supports_action(AccessibleAction action) const;
    [[nodiscard]] std::span<const AccessibleAction> actions() const;
    [[nodiscard]] bool perform_action(AccessibleAction action) const;

    /// Debug-visible semantic relations to other widgets.
    void set_relation(AccessibleRelationKind kind, std::string target_debug_name);
    void remove_relation(AccessibleRelationKind kind);
    [[nodiscard]] std::span<const AccessibleRelation> relations() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
