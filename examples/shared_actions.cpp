#include <memory>
#include <nk/actions/action.h>
#include <nk/actions/shortcut.h>
#include <nk/foundation/types.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/key_codes.h>
#include <nk/platform/native_menu.h>
#include <nk/platform/window.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/command_palette.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/label.h>
#include <nk/widgets/menu_bar.h>
#include <string>
#include <string_view>

int main(int argc, char** argv) {
    nk::Application app(argc, argv);
    nk::Window window({.title = "Shared actions", .width = 620, .height = 520});
    auto actions = std::make_shared<nk::ActionGroup>();
    auto save = std::make_shared<nk::Action>("file.save");
    auto details = std::make_shared<nk::Action>("view.details");
    details->set_checkable(true);
    actions->add(save);
    actions->add(details);
    window.set_action_group(actions);

#ifdef __APPLE__
    constexpr auto CommandModifier = nk::Modifiers::Super;
    constexpr auto MenuModifier = nk::NativeMenuModifier::Super;
#else
    constexpr auto CommandModifier = nk::Modifiers::Ctrl;
    constexpr auto MenuModifier = nk::NativeMenuModifier::Ctrl;
#endif
    window.add_shortcut({{static_cast<int>(nk::KeyCode::S), CommandModifier}, "file.save"});
    window.add_shortcut({{static_cast<int>(nk::KeyCode::D), CommandModifier}, "view.details"});

    auto menu = nk::MenuBar::create();
    menu->add_menu(
        {"File",
         {nk::MenuItem::action(
             "Save", "file.save", nk::NativeMenuShortcut{nk::KeyCode::S, MenuModifier})}});
    menu->add_menu({"View", {nk::MenuItem::action("Details", "view.details")}});
    menu->set_action_group(actions);

    auto save_button = nk::Button::create("Save");
    save_button->set_action(save);
    auto details_button = nk::Button::create("Details");
    details_button->set_action(details);
    auto enable_button = nk::Button::create("Enable / disable Save");
    auto enable_connection =
        enable_button->on_clicked().connect([save] { save->set_enabled(!save->is_enabled()); });
    auto toolbar = nk::Box::create(nk::Orientation::Horizontal, 8.0F);
    toolbar->append(save_button);
    toolbar->append(details_button);
    toolbar->append(enable_button);

    auto palette = nk::CommandPalette::create();
    palette->set_commands(
        {{.id = "file.save", .title = "Save", .subtitle = {}, .category = "File"},
         {.id = "view.details", .title = "Details", .subtitle = {}, .category = "View"}});
    palette->set_action_group(actions);

    int saves = 0;
    auto status = nk::Label::create("Saved 0 times");
    auto save_connection = save->on_activated().connect(
        [&saves, status] { status->set_text("Saved " + std::to_string(++saves) + " times"); });
    auto detail_status = nk::Label::create("Details off");
    auto detail_connection =
        details->on_activated().connect([weak = std::weak_ptr{details}, detail_status] {
            if (const auto action = weak.lock()) {
                detail_status->set_text(action->is_checked() ? "Details on" : "Details off");
            }
        });
    auto confirm = nk::Button::create("Open modal confirmation");
    auto dialog = nk::Dialog::create("Confirmation", "Window shortcuts are blocked here.");
    dialog->add_button("Close", nk::DialogResponse::Cancel);
    auto confirm_connection =
        confirm->on_clicked().connect([&window, dialog] { dialog->present(window); });

    auto root = nk::Box::create(nk::Orientation::Vertical, 12.0F);
    root->set_padding({12, 12, 12, 12});
    root->append(menu);
    root->append(toolbar);
    root->append(palette);
    root->append(status);
    root->append(detail_status);
    root->append(confirm);
    window.set_child(root);

    if (argc > 1 && std::string_view(argv[1]) == "--smoke-test") {
        const nk::KeyEvent enter{.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return};
        const nk::KeyEvent down{.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Down};
        menu->handle_key_event(down);
        menu->handle_key_event(down);
        menu->handle_key_event(enter);
        save_button->handle_key_event(enter);
        palette->handle_key_event(enter);
        window.dispatch_key_event({.type = nk::KeyEvent::Type::Press,
                                   .key = nk::KeyCode::S,
                                   .modifiers = CommandModifier});
        return saves == 4 ? 0 : 1;
    }

    window.present();
    return app.run();
}
