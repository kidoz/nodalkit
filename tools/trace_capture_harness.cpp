#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <nk/layout/box_layout.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/widgets/image_view.h>
#include <nk/widgets/label.h>
#include <nk/widgets/list_view.h>
#include <nk/widgets/text_field.h>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

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

std::vector<uint32_t> build_pattern(int width, int height, int seed) {
    std::vector<uint32_t> pixels(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto teal = static_cast<uint8_t>((x * 5 + seed * 19) & 0xFF);
            const auto green = static_cast<uint8_t>((y * 7 + seed * 13) & 0xFF);
            const auto blue = static_cast<uint8_t>(((x + y) * 3 + seed * 11) & 0xFF);
            pixels[static_cast<std::size_t>(y * width + x)] =
                0xFF000000U | (static_cast<uint32_t>(teal) << 16U) |
                (static_cast<uint32_t>(green) << 8U) | static_cast<uint32_t>(blue);
        }
    }
    return pixels;
}

int usage(const char* argv0) {
    std::cerr << "usage: " << (argv0 != nullptr ? argv0 : "nk_trace_capture_harness")
              << " <artifact-out.json> <trace-out.json>\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        return usage(argc > 0 ? argv[0] : "nk_trace_capture_harness");
    }

    const auto artifact_path = std::filesystem::path(argv[1]);
    const auto trace_path = std::filesystem::path(argv[2]);

    nk::Application app({
        .app_id = "org.nodalkit.trace_capture_harness",
        .app_name = "NodalKit Trace Capture Harness",
    });
    app.set_system_preferences_observation_enabled(false);

    nk::Window window({
        .title = "Trace Capture Harness",
        .width = 760,
        .height = 560,
    });

    auto root = Box::vertical(10.0F);
    root->set_debug_name("harness-root");

    auto title = nk::Label::create("Trace capture harness");
    title->set_debug_name("harness-title");
    auto text_field = nk::TextField::create("alpha");
    text_field->set_debug_name("harness-field");
    auto image_view = nk::ImageView::create();
    image_view->set_debug_name("harness-image");
    image_view->set_preserve_aspect_ratio(true);
    image_view->set_scale_mode(nk::ScaleMode::NearestNeighbor);
    auto image_pixels = build_pattern(96, 72, 0);
    image_view->update_pixel_buffer(image_pixels.data(), 96, 72);

    auto model = std::make_shared<nk::StringListModel>(
        std::vector<std::string>{"Alpha", "Bravo", "Charlie", "Delta"});
    auto selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    selection->select(0);
    auto list_view = nk::ListView::create();
    list_view->set_debug_name("harness-list");
    list_view->set_model(model);
    list_view->set_selection_model(selection);
    list_view->set_row_height(28.0F);

    root->append(title);
    root->append(text_field);
    root->append(image_view);
    root->append(list_view);

    window.set_child(root);
    window.present();
    if (!app.event_loop().poll()) {
        std::cerr << "initial poll did not process the first frame\n";
        return 1;
    }

    app.event_loop().clear_debug_trace_events();

    bool interval_ran = false;
    nk::CallbackHandle interval_handle{};

    app.event_loop().post(
        [&] {
            title->set_text("Trace capture phase 1");
            text_field->set_text("alpha beta gamma");
        },
        "harness.text-phase");

    (void)app.event_loop().set_timeout(
        std::chrono::milliseconds{0},
        [&] {
            model->append("Echo");
            selection->set_current_row(model->row_count() - 1);
            selection->select(model->row_count() - 1);
        },
        "harness.model-phase");

    interval_handle = app.event_loop().set_interval(
        std::chrono::milliseconds{1},
        [&] {
            if (interval_ran) {
                app.event_loop().cancel(interval_handle);
                return;
            }
            interval_ran = true;
            auto updated_pixels = build_pattern(96, 72, 1);
            image_view->update_pixel_buffer(updated_pixels.data(), 96, 72);
            app.event_loop().cancel(interval_handle);
        },
        "harness.image-phase");

    (void)app.event_loop().add_idle(
        [&] {
            title->set_text("Trace capture settled");
            text_field->set_text("stable capture payload");
            image_view->set_scale_mode(nk::ScaleMode::Bilinear);
        },
        "harness.idle-phase");

    for (int step = 0; step < 8; ++step) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        (void)app.event_loop().poll();
    }

    const auto history = window.debug_frame_history();
    if (history.size() < 2) {
        std::cerr << "expected at least two captured frames, got " << history.size() << "\n";
        return 1;
    }

    const auto trace_events = app.event_loop().debug_trace_events();
    if (trace_events.empty()) {
        std::cerr << "expected runtime trace events\n";
        return 1;
    }

    const auto artifact_save =
        window.save_frame_diagnostics_artifact_json_file(artifact_path.string());
    if (!artifact_save) {
        std::cerr << artifact_save.error() << "\n";
        return 1;
    }

    const auto trace_save = window.save_frame_trace_json_file(trace_path.string());
    if (!trace_save) {
        std::cerr << trace_save.error() << "\n";
        return 1;
    }

    const auto artifact = nk::load_frame_diagnostics_artifact_json_file(artifact_path.string());
    if (!artifact) {
        std::cerr << artifact.error() << "\n";
        return 1;
    }

    const auto trace = nk::load_frame_diagnostics_trace_json_file(trace_path.string());
    if (!trace) {
        std::cerr << trace.error() << "\n";
        return 1;
    }

    if (artifact->frames.size() != history.size()) {
        std::cerr << "artifact frame count mismatch\n";
        return 1;
    }

    const auto has_source = [&](std::string_view label) {
        return std::any_of(
            trace->events.begin(), trace->events.end(), [&](const nk::TraceEvent& event) {
                return event.source_label == label;
            });
    };

    if (!has_source("harness.text-phase") || !has_source("harness.model-phase") ||
        !has_source("harness.image-phase") || !has_source("harness.idle-phase")) {
        std::cerr << "trace capture is missing expected labeled harness events\n";
        return 1;
    }

    const auto histogram = nk::build_frame_time_histogram(artifact->frames);
    if (histogram.total_count() != artifact->frames.size()) {
        std::cerr << "frame histogram did not match artifact frame count\n";
        return 1;
    }

    return 0;
}
