/// @file search_input.cpp
/// @brief Interactive search editing and activation example.

#include <memory>
#include <nk/layout/box_layout.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/ui_core/widget.h>
#include <nk/widgets/label.h>
#include <nk/widgets/search_field.h>
#include <string>
#include <utility>

class SearchPanel : public nk::Widget {
public:
    static std::shared_ptr<SearchPanel> create() {
        auto panel = std::shared_ptr<SearchPanel>(new SearchPanel());
        panel->set_margin({16.0F, 16.0F, 16.0F, 16.0F});
        auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
        layout->set_spacing(12.0F);
        panel->set_layout_manager(std::move(layout));
        return panel;
    }

    void append(std::shared_ptr<nk::Widget> child) { append_child(std::move(child)); }

private:
    SearchPanel() = default;
};

int main(int argc, char** argv) {
    nk::Application app(argc, argv);
    nk::Window window({.title = "Search input", .width = 600, .height = 220});
    auto panel = SearchPanel::create();
    auto search = nk::SearchField::create("Search records");
    auto query = nk::Label::create("Query is empty");
    auto result = nk::Label::create("Press Enter to search");
    auto changed = search->on_text_changed().connect([query](std::string_view text) {
        query->set_text(text.empty() ? "Query is empty" : "Query: " + std::string(text));
    });
    auto activated = search->on_search().connect([result](std::string_view text) {
        result->set_text(text.empty() ? "Search submitted with an empty query"
                                      : "Submitted: " + std::string(text));
    });
    (void)changed;
    (void)activated;
    panel->append(nk::Label::create("Select, copy, paste, and undo with standard shortcuts."));
    panel->append(
        nk::Label::create("Escape or the clear button clears the query. Undo restores it."));
    panel->append(search);
    panel->append(query);
    panel->append(result);
    window.set_child(panel);
    window.present();
    search->grab_focus();
    return app.run();
}
