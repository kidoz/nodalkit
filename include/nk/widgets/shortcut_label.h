#pragma once

/// @file shortcut_label.h
/// @brief Text label for a keyboard shortcut.

#include <memory>
#include <nk/platform/native_menu.h>
#include <nk/ui_core/widget.h>
#include <string>

namespace nk {

/// Paints a `NativeMenuShortcut` as text, for example `Ctrl+Alt+Shift+S`.
///
/// Modifiers are always ordered Ctrl, Alt, Shift, then Super. An unknown key,
/// or a key with no written name, produces an empty label that measures as
/// empty and paints nothing. Menus draw `format()` so a shortcut reads the
/// same in a menu, a toolbar, or a preference row.
class ShortcutLabel : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ShortcutLabel> create(NativeMenuShortcut shortcut = {});
    ~ShortcutLabel() override;

    /// Written form of a shortcut. Empty when the key cannot be shown.
    [[nodiscard]] static std::string format(const NativeMenuShortcut& shortcut);

    [[nodiscard]] NativeMenuShortcut shortcut() const;
    void set_shortcut(NativeMenuShortcut shortcut);

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    explicit ShortcutLabel(NativeMenuShortcut shortcut);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
