#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nk/layout/box_layout.h>
#include <nk/platform/application.h>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <nk/widgets/button.h>
#include <nk/widgets/label.h>
#include <nk/widgets/text_field.h>
#include <string_view>

namespace {

void set_renderer_backend_env(std::string_view backend) {
#if defined(_WIN32)
    (void)_putenv_s("NK_RENDERER_BACKEND", std::string(backend).c_str());
#else
    (void)::setenv("NK_RENDERER_BACKEND", std::string(backend).c_str(), 1);
#endif
}

class Box : public nk::Widget {
public:
    static std::shared_ptr<Box> vertical(float spacing = 8.0F) {
        auto box = std::shared_ptr<Box>(new Box());
        auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Vertical);
        layout->set_spacing(spacing);
        box->set_layout_manager(std::move(layout));
        return box;
    }

    void append(std::shared_ptr<nk::Widget> child) { append_child(std::move(child)); }

private:
    Box() = default;
};

bool file_starts_with_ppm_header(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return false;
    }

    std::string header;
    std::getline(in, header);
    return header == "P6";
}

int usage(const char* argv0) {
    std::cerr << "usage: " << (argv0 != nullptr ? argv0 : "nk_overlay_screenshot_harness")
              << " <layout-bounds.ppm> <dirty-widgets.ppm> <inspector-overlay.ppm>"
              << " <inspector-docked.ppm> <widget-filter.ppm> <render-focus.ppm>\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 7) {
        return usage(argc > 0 ? argv[0] : "nk_overlay_screenshot_harness");
    }

    const auto layout_path = std::filesystem::path(argv[1]);
    const auto dirty_path = std::filesystem::path(argv[2]);
    const auto inspector_path = std::filesystem::path(argv[3]);
    const auto docked_path = std::filesystem::path(argv[4]);
    const auto filter_path = std::filesystem::path(argv[5]);
    const auto render_focus_path = std::filesystem::path(argv[6]);

    set_renderer_backend_env("software");

    nk::Application app({
        .app_id = "org.nodalkit.overlay_screenshot_harness",
        .app_name = "NodalKit Overlay Screenshot Harness",
    });
    app.set_system_preferences_observation_enabled(false);

    nk::Window window({
        .title = "Overlay Screenshot Harness",
        .width = 720,
        .height = 480,
    });

    auto root = Box::vertical(12.0F);
    root->set_debug_name("overlay-root");
    auto title = nk::Label::create("Overlay capture");
    title->set_debug_name("overlay-title");
    auto button = nk::Button::create("Action");
    button->set_debug_name("overlay-button");
    auto field = nk::TextField::create("alpha");
    field->set_debug_name("overlay-field");

    root->append(title);
    root->append(button);
    root->append(field);

    window.set_child(root);
    window.present();
    if (!app.event_loop().poll()) {
        std::cerr << "failed to process initial frame\n";
        return 1;
    }

    window.set_debug_overlay_flags(nk::DebugOverlayFlags::LayoutBounds);
    if (!app.event_loop().poll()) {
        std::cerr << "failed to render layout-bounds overlay frame\n";
        return 1;
    }
    const auto save_layout = window.save_debug_screenshot_ppm_file(layout_path.string());
    if (!save_layout) {
        std::cerr << save_layout.error() << "\n";
        return 1;
    }

    title->queue_redraw();
    window.set_debug_overlay_flags(nk::DebugOverlayFlags::DirtyWidgets);
    if (!app.event_loop().poll()) {
        std::cerr << "failed to render dirty-widgets overlay frame\n";
        return 1;
    }
    const auto save_dirty = window.save_debug_screenshot_ppm_file(dirty_path.string());
    if (!save_dirty) {
        std::cerr << save_dirty.error() << "\n";
        return 1;
    }

    window.set_debug_selected_widget(button.get());
    window.set_debug_overlay_flags(nk::DebugOverlayFlags::LayoutBounds |
                                   nk::DebugOverlayFlags::InspectorPanel);
    if (!app.event_loop().poll()) {
        std::cerr << "failed to render inspector overlay frame\n";
        return 1;
    }
    const auto save_inspector = window.save_debug_screenshot_ppm_file(inspector_path.string());
    if (!save_inspector) {
        std::cerr << save_inspector.error() << "\n";
        return 1;
    }

    window.set_debug_inspector_presentation(nk::DebugInspectorPresentation::DockedRight);
    if (!app.event_loop().poll()) {
        std::cerr << "failed to render docked inspector frame\n";
        return 1;
    }
    const auto save_docked = window.save_debug_screenshot_ppm_file(docked_path.string());
    if (!save_docked) {
        std::cerr << save_docked.error() << "\n";
        return 1;
    }

    window.set_debug_widget_filter("button");
    if (!app.event_loop().poll()) {
        std::cerr << "failed to render filtered inspector frame\n";
        return 1;
    }
    const auto save_filter = window.save_debug_screenshot_ppm_file(filter_path.string());
    if (!save_filter) {
        std::cerr << save_filter.error() << "\n";
        return 1;
    }

    window.set_debug_widget_filter({});
    window.dispatch_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::PageDown});
    if (!app.event_loop().poll()) {
        std::cerr << "failed to render focused render-node frame\n";
        return 1;
    }
    const auto save_render_focus =
        window.save_debug_screenshot_ppm_file(render_focus_path.string());
    if (!save_render_focus) {
        std::cerr << save_render_focus.error() << "\n";
        return 1;
    }

    for (const auto& path :
         {layout_path, dirty_path, inspector_path, docked_path, filter_path, render_focus_path}) {
        if (!std::filesystem::exists(path) || std::filesystem::file_size(path) == 0 ||
            !file_starts_with_ppm_header(path)) {
            std::cerr << "invalid screenshot output: " << path << "\n";
            return 1;
        }
    }

    return 0;
}
