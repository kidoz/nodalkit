#pragma once

/// @file button_content.h
/// @brief Icon, label, and menu indicator measured as one piece.

#include <memory>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

/// A symbolic icon drawn beside a button label.
enum class SymbolicIcon {
    None,
    Search,
    Add,
    Close,
};

/// The inside of a button: an icon, a label, and an optional menu chevron.
///
/// The icon and the label share one vertical center, with a fixed gap. The
/// chevron sits on the trailing edge. No icon and no label measures as empty
/// and paints nothing, even if the indicator is requested. `MenuButton` and
/// `SplitButton` use this for their label and chevron. A header bar can also
/// place one in a leading or trailing slot.
class ButtonContent : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<ButtonContent>
    create(std::string label = {}, SymbolicIcon icon = SymbolicIcon::None);
    ~ButtonContent() override;

    [[nodiscard]] std::string_view label() const;
    void set_label(std::string label);

    [[nodiscard]] SymbolicIcon icon() const;
    void set_icon(SymbolicIcon icon);

    [[nodiscard]] bool shows_indicator() const;
    void set_shows_indicator(bool show);

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;

protected:
    ButtonContent(std::string label, SymbolicIcon icon);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
