/// @file counter.cpp
/// @brief Counter example: Button increments a label.

#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/widgets/box.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>
#include <string>

int main(int argc, char** argv) {
    nk::Application app(argc, argv);

    nk::Window window({
        .title = "Counter",
        .width = 420,
        .height = 220,
    });

    int count = 0;

    auto root = nk::Box::create(nk::Orientation::Vertical, 8.0F);
    auto label = nk::Label::create("0");
    auto button = nk::Button::create("Increment");

    auto click_conn = button->on_clicked().connect([&count, &label](/**/) {
        ++count;
        label->set_text(std::to_string(count));
    });
    (void)click_conn;

    root->append(label);
    root->append(button);

    window.set_child(root);
    window.present();

    // Quit immediately for the scaffold.
    app.event_loop().post([&app] { app.quit(0); });

    return app.run();
}
