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
