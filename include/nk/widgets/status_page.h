#pragma once

/// @file status_page.h
/// @brief Centered empty, error, or welcome state.

#include <memory>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

class StatusPage : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<StatusPage> create(std::string title = {},
                                                            std::string description = {});
    ~StatusPage() override;

    [[nodiscard]] std::string_view title() const;
    void set_title(std::string title);
    [[nodiscard]] std::string_view description() const;
    void set_description(std::string description);
    /// Sets an optional illustration displayed above the title.
    void set_icon(std::shared_ptr<Widget> icon);
    [[nodiscard]] Widget* icon() const;
    void set_action(std::shared_ptr<Widget> action);
    [[nodiscard]] Widget* action() const;

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;

protected:
    StatusPage(std::string title, std::string description);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
