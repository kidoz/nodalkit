#pragma once

/// @file banner.h
/// @brief Persistent contextual message with an optional action.
///
/// `set_revealed(false)` collapses the banner through a `Revealer`. Reduced
/// motion hides it on that call. When motion is allowed, the height slides
/// closed and the banner stays visible until the slide finishes.

#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <string_view>

namespace nk {

class Banner : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Banner> create(std::string title = {});
    ~Banner() override;

    [[nodiscard]] std::string_view title() const;
    void set_title(std::string title);
    [[nodiscard]] std::string_view button_label() const;
    void set_button_label(std::string label);
    [[nodiscard]] bool is_revealed() const;
    void set_revealed(bool revealed);
    Signal<>& on_button_clicked();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;

protected:
    explicit Banner(std::string title);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
