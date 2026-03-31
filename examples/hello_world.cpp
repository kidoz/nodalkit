/// @file hello_world.cpp
/// @brief Minimal NodalKit application: a window with a label.

#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/widgets/label.h>

int main(int argc, char** argv) {
    nk::Application app(argc, argv);

    nk::Window window({
        .title = "Hello NodalKit",
        .width = 400,
        .height = 200,
    });

    auto label = nk::Label::create("Hello, World!");
    window.set_child(label);
    window.present();

    // Quit immediately for the scaffold (no real platform backend yet).
    app.event_loop().post([&app] { app.quit(0); });

    return app.run();
}
