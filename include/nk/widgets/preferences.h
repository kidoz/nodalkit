#pragma once

/// @file preferences.h
/// @brief GNOME-style preference rows, groups, and pages.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// One preference with a title, optional subtitle, and optional suffix control.
class PreferencesRow : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<PreferencesRow> create(std::string title,
                                                                std::string subtitle = {});
    ~PreferencesRow() override;

    [[nodiscard]] std::string_view title() const;
    void set_title(std::string title);
    [[nodiscard]] std::string_view subtitle() const;
    void set_subtitle(std::string subtitle);

    void set_suffix(std::shared_ptr<Widget> suffix);
    [[nodiscard]] Widget* suffix() const;

    [[nodiscard]] bool is_activatable() const;
    void set_activatable(bool activatable);
    Signal<>& on_activated();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;

protected:
    PreferencesRow(std::string title, std::string subtitle);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// A boxed-list row whose control is a switch.
///
/// Per the HIG, the row background activates the control: clicking anywhere on
/// the row toggles the switch. Keyboard focus lands on the switch rather than
/// the row, so users tab between controls instead of through inert rows.
class SwitchRow : public PreferencesRow {
public:
    [[nodiscard]] static std::shared_ptr<SwitchRow> create(std::string title,
                                                           std::string subtitle = {});
    ~SwitchRow() override;

    [[nodiscard]] bool is_active() const;
    void set_active(bool active);
    Signal<bool>& on_toggled();

protected:
    SwitchRow(std::string title, std::string subtitle);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// A boxed-list row whose control is a drop-down selection.
class ComboRow : public PreferencesRow {
public:
    [[nodiscard]] static std::shared_ptr<ComboRow> create(std::string title,
                                                          std::string subtitle = {});
    ~ComboRow() override;

    void set_items(std::vector<std::string> items);
    [[nodiscard]] int selected_index() const;
    void set_selected_index(int index);
    Signal<int>& on_selection_changed();

protected:
    ComboRow(std::string title, std::string subtitle);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// A boxed-list row whose control is an inline text entry.
class EntryRow : public PreferencesRow {
public:
    [[nodiscard]] static std::shared_ptr<EntryRow> create(std::string title,
                                                          std::string subtitle = {});
    ~EntryRow() override;

    [[nodiscard]] std::string_view text() const;
    void set_text(std::string text);
    void set_placeholder(std::string placeholder);
    Signal<std::string_view>& on_text_changed();

protected:
    EntryRow(std::string title, std::string subtitle);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// A preference row that navigates instead of editing a value.
///
/// The row is activatable immediately. A click, Return, or Space emits
/// `on_activated()`. A trailing chevron is shown, and there is no suffix
/// editor. The accessible role is a button and the accessible name is the
/// title. Add it to a `PreferencesGroup` the same way as a `SwitchRow`.
class ActionRow : public PreferencesRow {
public:
    [[nodiscard]] static std::shared_ptr<ActionRow> create(std::string title,
                                                           std::string subtitle = {});
    ~ActionRow() override;

protected:
    ActionRow(std::string title, std::string subtitle);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// A preference row that reveals one child, indented underneath.
///
/// The row shows a title, an optional subtitle, and a disclosure indicator.
/// `set_expanded(true)` shows the child. `set_expanded(false)` hides it and
/// drops its height so following rows move up. A click on the header, Return,
/// or Space toggles it and emits `on_expanded_changed()`. The row does not
/// edit a value. Add it to a `PreferencesGroup` the same way as an `ActionRow`.
class ExpanderRow : public PreferencesRow {
public:
    [[nodiscard]] static std::shared_ptr<ExpanderRow> create(std::string title,
                                                             std::string subtitle = {});
    ~ExpanderRow() override;

    [[nodiscard]] bool is_expanded() const;
    void set_expanded(bool expanded);

    void set_child(std::shared_ptr<Widget> child);
    [[nodiscard]] Widget* child() const;

    Signal<bool>& on_expanded_changed();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;

protected:
    ExpanderRow(std::string title, std::string subtitle);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    [[nodiscard]] float header_height() const;
};

/// A titled collection of preference rows.
class PreferencesGroup : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<PreferencesGroup> create(std::string title = {});
    ~PreferencesGroup() override;

    [[nodiscard]] std::string_view title() const;
    void set_title(std::string title);
    void add(std::shared_ptr<PreferencesRow> row);
    void remove(PreferencesRow& row);
    [[nodiscard]] std::span<const std::shared_ptr<PreferencesRow>> rows() const;

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    explicit PreferencesGroup(std::string title);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// A clamped, scroll-ready collection of preference groups suitable for use as
/// a Dialog's custom content.
class PreferencesPage : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<PreferencesPage> create(std::string title = {},
                                                                 std::string description = {});
    ~PreferencesPage() override;

    [[nodiscard]] std::string_view title() const;
    void set_title(std::string title);
    [[nodiscard]] std::string_view description() const;
    void set_description(std::string description);

    void add(std::shared_ptr<PreferencesGroup> group);
    void remove(PreferencesGroup& group);
    [[nodiscard]] std::span<const std::shared_ptr<PreferencesGroup>> groups() const;

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    PreferencesPage(std::string title, std::string description);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
