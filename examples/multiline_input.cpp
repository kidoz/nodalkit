/// @file multiline_input.cpp
/// @brief Multiline viewport, navigation, and read-only interaction example.

#include <memory>
#include <nk/foundation/types.h>
#include <nk/layout/box_layout.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>
#include <nk/widgets/text_area.h>
#include <string>
#include <utility>

namespace {

class EditorPanel : public nk::Widget {
public:
    static std::shared_ptr<EditorPanel> create() {
        auto panel = std::shared_ptr<EditorPanel>(new EditorPanel());
        auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
        layout->set_spacing(12.0F);
        panel->set_margin({16, 16, 16, 16});
        panel->set_layout_manager(std::move(layout));
        return panel;
    }

    void append(std::shared_ptr<nk::Widget> child) { append_child(std::move(child)); }

private:
    EditorPanel() = default;
};

} // namespace

int main(int argc, char** argv) {
    nk::Application app(argc, argv);
    nk::Window window({.title = "Multiline input", .width = 640, .height = 420});
    auto panel = EditorPanel::create();
    auto editor = nk::TextArea::create();
    editor->set_visible_rows(8);
    editor->set_placeholder("Write something");
    std::string sample;
    for (int line = 1; line <= 30; ++line) {
        sample += "Line " + std::to_string(line) + ": ";
        sample += line % 3 == 0 ? "A long line for horizontal scrolling. " + std::string(80, 'w')
                                : "Short text: caf\u00E9, \u4E16\u754C.";
        sample += '\n';
    }
    editor->set_text(std::move(sample));
    auto toggle = nk::Button::create("Make read-only");
    auto toggle_connection =
        toggle->on_clicked().connect([editor, weak_toggle = std::weak_ptr(toggle)] {
            editor->set_editable(!editor->is_editable());
            if (const auto button = weak_toggle.lock()) {
                button->set_label(editor->is_editable() ? "Make read-only" : "Make editable");
            }
            editor->grab_focus();
        });
    (void)toggle_connection;
    panel->append(
        nk::Label::create("Drag or Shift + arrows to select. Double-click selects a word."));
    panel->append(nk::Label::create("Use standard copy, cut, paste, and undo shortcuts."));
    panel->append(nk::Label::create("Scroll long lines horizontally with Shift + wheel."));
    panel->append(nk::Label::create(
        "Home / End moves within a line; Control + Home / End moves through the document."));
    panel->append(editor);
    panel->append(toggle);
    window.set_child(panel);
    window.present();
    editor->grab_focus();
    return app.run();
}
