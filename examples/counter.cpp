/// @file counter.cpp
/// @brief Counter example: Button increments a label.

#include <nk/layout/box_layout.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>

#include <string>

/// A simple container widget that uses a BoxLayout.
class Box : public nk::Widget {
public:
    static std::shared_ptr<Box> vertical(float spacing = 8.0F) {
        auto box = std::shared_ptr<Box>(new Box());
        auto layout = std::make_unique<nk::BoxLayout>(
            nk::Orientation::Vertical);
        layout->set_spacing(spacing);
        box->set_layout_manager(std::move(layout));
        return box;
    }

    void append(std::shared_ptr<nk::Widget> child) {
        append_child(std::move(child));
    }

private:
    Box() = default;
};

int main(int argc, char** argv) {
    nk::Application app(argc, argv);

    nk::Window window({
        .title = "Counter",
        .width = 420,
        .height = 220,
    });

    int count = 0;

    auto root = Box::vertical();
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
