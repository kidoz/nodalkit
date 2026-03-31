#pragma once

/// @file dialog.h
/// @brief Async dialog abstraction.

#include <nk/foundation/signal.h>
#include <nk/ui_core/widget.h>

#include <functional>
#include <memory>
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
    [[nodiscard]] static std::shared_ptr<Dialog> create(
        std::string title, std::string message = {});

    ~Dialog() override;

    /// Add a response button.
    void add_button(std::string label, DialogResponse response);

    /// Set the content widget (replaces the message label).
    void set_content(std::shared_ptr<Widget> content);

    /// Present the dialog modally relative to the given window.
    void present(Window& parent);

    /// Close the dialog, emitting the given response.
    void close(DialogResponse response = DialogResponse::Close);

    /// Emitted when the user responds.
    Signal<DialogResponse>& on_response();

protected:
    Dialog(std::string title, std::string message);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nk
