#pragma once

/// @file dialog.h
/// @brief Async dialog abstraction.

#include <functional>
#include <memory>
#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>
#include <string>
#include <vector>

namespace nk {

class Window;

/// Standard dialog response codes.
enum class DialogResponse {
    None,
    Accept,
    Cancel,
    Close,
    Custom, ///< Application-defined response.
};

/// Visual presentation policy for dialogs.
enum class DialogPresentationStyle {
    Default,
    Sheet,
};

/// Async dialog. Dialogs are shown with present() and emit
/// on_response() when the user acts. No blocking show() call.
///
/// Usage:
/// @code
///   auto dlg = nk::Dialog::create("Confirm", "Save changes?");
///   dlg->add_button("Cancel", nk::DialogResponse::Cancel);
///   dlg->add_button("Save",   nk::DialogResponse::Accept);
///   dlg->on_response().connect([](nk::DialogResponse r) { ... });
///   dlg->present(parent_window);
/// @endcode
class Dialog : public Widget {
public:
    [[nodiscard]] static std::shared_ptr<Dialog> create(std::string title,
                                                        std::string message = {});

    ~Dialog() override;

    /// Add a response button.
    void add_button(std::string label, DialogResponse response);

    /// Set the content widget (replaces the message label).
    void set_content(std::shared_ptr<Widget> content);

    /// Set the visual presentation style used when the dialog is shown.
    void set_presentation_style(DialogPresentationStyle style);

    /// Set the minimum panel width used during measure and allocation.
    void set_minimum_panel_width(float width);

    /// Present the dialog modally relative to the given window.
    void present(Window& parent);
    [[nodiscard]] bool is_presented() const;

    /// Close the dialog, emitting the given response.
    void close(DialogResponse response = DialogResponse::Close);

    /// Emitted when the user responds.
    Signal<DialogResponse>& on_response();

    [[nodiscard]] SizeRequest measure(const Constraints& constraints) const override;
    void allocate(const Rect& allocation) override;
    bool handle_mouse_event(const MouseEvent& event) override;
    bool handle_key_event(const KeyEvent& event) override;
    [[nodiscard]] bool hit_test(Point point) const override;
    [[nodiscard]] std::vector<Rect> damage_regions() const override;

protected:
    Dialog(std::string title, std::string message);
    void snapshot(SnapshotContext& ctx) const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
