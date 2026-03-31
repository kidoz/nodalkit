#include <nk/widgets/dialog.h>

#include <nk/platform/window.h>

namespace nk {

struct Dialog::Impl {
    std::string title;
    std::string message;
    std::shared_ptr<Widget> content;

    struct ButtonEntry {
        std::string label;
        DialogResponse response;
    };
    std::vector<ButtonEntry> buttons;

    Signal<DialogResponse> on_response;
};

std::shared_ptr<Dialog> Dialog::create(
    std::string title, std::string message) {
    return std::shared_ptr<Dialog>(
        new Dialog(std::move(title), std::move(message)));
}

Dialog::Dialog(std::string title, std::string message)
    : impl_(std::make_unique<Impl>()) {
    impl_->title = std::move(title);
    impl_->message = std::move(message);
    add_style_class("dialog");
}

Dialog::~Dialog() = default;

void Dialog::add_button(std::string label, DialogResponse response) {
    impl_->buttons.push_back({std::move(label), response});
}

void Dialog::set_content(std::shared_ptr<Widget> content) {
    impl_->content = std::move(content);
}

void Dialog::present(Window& /*parent*/) {
    // Stub: a real implementation creates a platform dialog surface
    // or a modal overlay within the parent window.
}

void Dialog::close(DialogResponse response) {
    impl_->on_response.emit(response);
}

Signal<DialogResponse>& Dialog::on_response() {
    return impl_->on_response;
}

} // namespace nk
