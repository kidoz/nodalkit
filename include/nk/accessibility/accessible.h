#pragma once

/// @file accessible.h
/// @brief Accessibility metadata attached to widgets.

#include <nk/accessibility/role.h>
#include <nk/ui_core/state_flags.h>

#include <memory>
#include <string>
#include <string_view>

namespace nk {

/// Provides accessibility information for a widget.
/// Each widget can have an Accessible that exposes its role,
/// name, description, and live state to assistive technologies.
class Accessible {
public:
    Accessible();
    ~Accessible();

    Accessible(Accessible const&) = delete;
    Accessible& operator=(Accessible const&) = delete;

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
