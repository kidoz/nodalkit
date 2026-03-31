#include <nk/foundation/signal.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>

int main() {
    nk::Signal<int> signal;
    int observed = 0;

    auto connection = signal.connect([&observed](int value) { observed = value; });
    signal.emit(42);

    auto label = nk::Label::create("Installed SDK");
    auto button = nk::Button::create("Press");

    if (!connection.connected()) {
        return 1;
    }
    if (observed != 42) {
        return 1;
    }
    if (label->text() != "Installed SDK") {
        return 1;
    }
    if (button->label() != "Press") {
        return 1;
    }

    return 0;
}
