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

/// Async toolkit-level modal dialog.
///
/// Dialogs are **toolkit-level modal overlays** composited into the parent
/// window — not native OS dialogs. `present()` returns immediately; the
/// response arrives asynchronously via `on_response()`. There is no
/// blocking show() call.
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

    /// Add a response button. Buttons are drawn in insertion order.
    /// Keyboard activation uses the response kind: Escape picks the first
    /// Cancel button (fallback Close), Return/Space picks the first Accept
    /// button (fallback: the first button, or Close if none).
    void add_button(std::string label, DialogResponse response);

    /// Replace the default message label with a custom content widget.
    /// The dialog continues to render its title and button row around it.
    void set_content(std::shared_ptr<Widget> content);

    /// Set the visual presentation style (Default or Sheet). Takes effect
    /// at the next layout.
    void set_presentation_style(DialogPresentationStyle style);

    /// Set the minimum panel width in logical pixels. Clamped to at least
    /// 120 by the implementation. Defaults to 280.
    void set_minimum_panel_width(float width);

    /// Present the dialog as a modal overlay on the given parent window.
    /// Idempotent for the same parent — repeated calls with the same
    /// window are a no-op. Re-presenting on a different parent dismisses
    /// from the old parent first. Focus is saved from the parent on
    /// present and restored on close. Returns immediately; the response
    /// arrives via on_response().
    void present(Window& parent);
    [[nodiscard]] bool is_presented() const;

    /// Close the dialog, dismissing the overlay and emitting `response`
    /// via on_response(). Safe to call when not presented — in that case
    /// no overlay is dismissed but on_response still fires.
    void close(DialogResponse response = DialogResponse::Close);

    /// Emitted from close() with the chosen DialogResponse. Slots are
    /// invoked synchronously from the event loop during key/mouse dispatch
    /// (or from the caller's thread when close() is called directly).
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
