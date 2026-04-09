#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <nk/layout/box_layout.h>
#include <nk/model/abstract_list_model.h>
#include <nk/model/selection_model.h>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/render/renderer.h>
#include <nk/widgets/image_view.h>
#include <nk/widgets/label.h>
#include <nk/widgets/list_view.h>
#include <nk/widgets/text_field.h>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

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

    static std::shared_ptr<Box> horizontal(float spacing = 8.0F) {
        auto box = std::shared_ptr<Box>(new Box());
        auto layout = std::make_unique<nk::BoxLayout>(nk::Orientation::Horizontal);
        layout->set_spacing(spacing);
        box->set_layout_manager(std::move(layout));
        return box;
    }

    void append(std::shared_ptr<nk::Widget> child) { append_child(std::move(child)); }

private:
    Box() = default;
};

enum class Scenario {
    Mixed,
    TextHeavy,
    ListHeavy,
    AnimationHeavy,
    LocalizedRedraw,
};

struct HarnessScene {
    std::shared_ptr<Box> root;
    std::shared_ptr<nk::Label> title;
    std::shared_ptr<nk::TextField> text_field;
    std::shared_ptr<nk::ImageView> image_view;
    std::shared_ptr<nk::ImageView> secondary_image_view;
    std::shared_ptr<nk::StringListModel> model;
    std::shared_ptr<nk::SelectionModel> selection;
    std::shared_ptr<nk::ListView> list_view;
    std::vector<std::shared_ptr<nk::Label>> labels;
};

struct BaselinePaths {
    std::filesystem::path artifact;
    std::filesystem::path trace;
};

struct HarnessOptions {
    std::string backend = "software";
    std::optional<BaselinePaths> baselines;
};

struct ArtifactSummary {
    std::size_t frame_count = 0;
    double max_total_ms = 0.0;
    std::size_t max_render_node_count = 0;
    std::size_t max_text_shape_count = 0;
    std::size_t max_image_texture_upload_count = 0;
    std::size_t max_model_row_materialize_count = 0;
};

struct TraceSummary {
    double max_frame_ms = 0.0;
    std::size_t over_budget_frame_count = 0;
    std::size_t slow_frame_count = 0;
    std::size_t very_slow_frame_count = 0;
    std::size_t posted_task_count = 0;
    std::size_t timeout_count = 0;
    std::size_t interval_count = 0;
    std::size_t idle_count = 0;
    std::size_t unlabeled_runtime_event_count = 0;
};

struct StableScenarioThresholds {
    double max_artifact_total_ms_regression = 1.5;
    double max_trace_frame_ms_regression = 1.5;
    std::size_t max_render_node_regression = 2;
    std::size_t max_text_shape_regression = 2;
    std::size_t max_image_upload_regression = 1;
    std::size_t max_model_materialize_regression = 1;
    std::size_t max_over_budget_frame_regression = 0;
    std::size_t max_slow_frame_regression = 0;
    std::size_t max_very_slow_frame_regression = 0;
    std::size_t max_posted_task_regression = 1;
    std::size_t max_timeout_regression = 0;
    std::size_t max_interval_regression = 0;
    std::size_t max_idle_regression = 0;
    std::size_t max_unlabeled_runtime_regression = 0;
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

std::vector<std::string> make_items(std::string_view prefix, int count) {
    std::vector<std::string> items;
    items.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        items.emplace_back(std::string(prefix) + " item " + std::to_string(index));
    }
    return items;
}

std::string scenario_name(Scenario scenario) {
    switch (scenario) {
    case Scenario::Mixed:
        return "mixed";
    case Scenario::TextHeavy:
        return "text-heavy";
    case Scenario::ListHeavy:
        return "list-heavy";
    case Scenario::AnimationHeavy:
        return "animation-heavy";
    case Scenario::LocalizedRedraw:
        return "localized-redraw";
    }
    return "mixed";
}

nk::Result<Scenario> parse_scenario(std::string_view value) {
    if (value == "mixed") {
        return Scenario::Mixed;
    }
    if (value == "text-heavy") {
        return Scenario::TextHeavy;
    }
    if (value == "list-heavy") {
        return Scenario::ListHeavy;
    }
    if (value == "animation-heavy") {
        return Scenario::AnimationHeavy;
    }
    if (value == "localized-redraw") {
        return Scenario::LocalizedRedraw;
    }
    return nk::Unexpected("unknown scenario: " + std::string(value));
}

int usage(const char* argv0) {
    std::cerr
        << "usage: " << (argv0 != nullptr ? argv0 : "nk_trace_capture_harness")
        << " <mixed|text-heavy|list-heavy|animation-heavy|localized-redraw> <artifact-out.json>"
        << " <trace-out.json> [--backend=<software|metal>] [--baseline-artifact=<path>]"
        << " [--baseline-trace=<path>]\n";
    return 2;
}

HarnessScene build_scene(Scenario scenario) {
    HarnessScene scene{};
    scene.root = Box::vertical(10.0F);
    scene.root->set_debug_name("harness-root");

    scene.title = nk::Label::create("Trace capture harness");
    scene.title->set_debug_name("harness-title");
    scene.text_field = nk::TextField::create("alpha");
    scene.text_field->set_debug_name("harness-field");
    scene.image_view = nk::ImageView::create();
    scene.image_view->set_debug_name("harness-image");
    scene.image_view->set_preserve_aspect_ratio(true);
    scene.image_view->set_scale_mode(nk::ScaleMode::NearestNeighbor);
    auto image_pixels = build_pattern(96, 72, 0);
    scene.image_view->update_pixel_buffer(image_pixels.data(), 96, 72);
    scene.secondary_image_view = nk::ImageView::create();
    scene.secondary_image_view->set_debug_name("harness-image-secondary");
    scene.secondary_image_view->set_preserve_aspect_ratio(true);
    scene.secondary_image_view->set_scale_mode(nk::ScaleMode::NearestNeighbor);
    auto secondary_pixels = build_pattern(96, 72, 7);
    scene.secondary_image_view->update_pixel_buffer(secondary_pixels.data(), 96, 72);

    scene.model = std::make_shared<nk::StringListModel>(make_items("Harness", 6));
    scene.selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
    scene.selection->select(0);
    scene.list_view = nk::ListView::create();
    scene.list_view->set_debug_name("harness-list");
    scene.list_view->set_model(scene.model);
    scene.list_view->set_selection_model(scene.selection);
    scene.list_view->set_row_height(28.0F);

    scene.root->append(scene.title);

    switch (scenario) {
    case Scenario::Mixed:
        scene.root->append(scene.text_field);
        scene.root->append(scene.image_view);
        scene.root->append(scene.list_view);
        break;
    case Scenario::TextHeavy:
        scene.root->append(scene.text_field);
        for (int index = 0; index < 14; ++index) {
            auto label = nk::Label::create("Text-heavy baseline line " + std::to_string(index));
            label->set_debug_name("harness-text-" + std::to_string(index));
            scene.labels.push_back(label);
            scene.root->append(label);
        }
        break;
    case Scenario::ListHeavy:
        scene.model = std::make_shared<nk::StringListModel>(make_items("List-heavy", 48));
        scene.selection = std::make_shared<nk::SelectionModel>(nk::SelectionMode::Single);
        scene.selection->select(0);
        scene.list_view->set_model(scene.model);
        scene.list_view->set_selection_model(scene.selection);
        scene.list_view->set_row_height(24.0F);
        scene.root->append(scene.list_view);
        scene.root->append(scene.text_field);
        break;
    case Scenario::AnimationHeavy:
        scene.root->append(scene.image_view);
        scene.root->append(scene.text_field);
        break;
    case Scenario::LocalizedRedraw: {
        auto row = Box::horizontal(16.0F);
        row->set_debug_name("localized-redraw-row");
        row->append(scene.image_view);
        row->append(scene.secondary_image_view);
        scene.root->append(row);
        scene.root->append(scene.text_field);
        break;
    }
    }

    return scene;
}

template <typename Fn>
std::size_t sum_frame_counter(std::span<const nk::FrameDiagnostics> frames, Fn&& access) {
    std::size_t total = 0;
    for (const auto& frame : frames) {
        total += access(frame);
    }
    return total;
}

bool trace_has_source(std::span<const nk::TraceEvent> events, std::string_view label) {
    return std::any_of(events.begin(), events.end(), [&](const nk::TraceEvent& event) {
        return event.source_label == label;
    });
}

std::size_t trace_source_count(std::span<const nk::TraceEvent> events, std::string_view label) {
    return static_cast<std::size_t>(
        std::count_if(events.begin(), events.end(), [&](const nk::TraceEvent& event) {
            return event.source_label == label;
        }));
}

template <typename T>
bool check_regression(std::string_view label,
                      T baseline,
                      T candidate,
                      T allowed_regression,
                      std::string_view scenario_label) {
    if (candidate <= baseline + allowed_regression) {
        return false;
    }

    std::cerr << "stable scenario regression [" << scenario_label << "]: " << label
              << " baseline=" << baseline << " candidate=" << candidate
              << " allowed=" << allowed_regression << "\n";
    return true;
}

ArtifactSummary summarize_artifact(const nk::FrameDiagnosticsArtifact& artifact) {
    ArtifactSummary summary;
    summary.frame_count = artifact.frames.size();
    for (const auto& frame : artifact.frames) {
        summary.max_total_ms = std::max(summary.max_total_ms, frame.total_ms);
        summary.max_render_node_count =
            std::max(summary.max_render_node_count, frame.render_node_count);
        summary.max_text_shape_count =
            std::max(summary.max_text_shape_count, frame.render_hotspot_counters.text_shape_count);
        summary.max_image_texture_upload_count =
            std::max(summary.max_image_texture_upload_count,
                     frame.render_hotspot_counters.image_texture_upload_count);
        summary.max_model_row_materialize_count =
            std::max(summary.max_model_row_materialize_count,
                     frame.widget_hotspot_totals.model_row_materialize_count);
    }
    return summary;
}

TraceSummary summarize_trace(const nk::TraceCapture& trace) {
    TraceSummary summary;
    for (const auto& event : trace.events) {
        if (event.name == "frame") {
            summary.max_frame_ms = std::max(summary.max_frame_ms, event.duration_ms);
            switch (nk::classify_frame_time(event.duration_ms)) {
            case nk::FramePerformanceMarker::WithinBudget:
                break;
            case nk::FramePerformanceMarker::OverBudget:
                ++summary.over_budget_frame_count;
                break;
            case nk::FramePerformanceMarker::Slow:
                ++summary.slow_frame_count;
                break;
            case nk::FramePerformanceMarker::VerySlow:
                ++summary.very_slow_frame_count;
                break;
            }
        } else if (event.name == "posted-task") {
            ++summary.posted_task_count;
        } else if (event.name == "timeout") {
            ++summary.timeout_count;
        } else if (event.name == "interval") {
            ++summary.interval_count;
        } else if (event.name == "idle") {
            ++summary.idle_count;
        }

        if ((event.category == "event-loop-task" || event.category == "event-loop-timer" ||
             event.category == "event-loop-idle") &&
            event.source_label.empty()) {
            ++summary.unlabeled_runtime_event_count;
        }
    }
    return summary;
}

bool validate_against_stable_baseline(Scenario scenario,
                                      nk::RendererBackend backend,
                                      const nk::FrameDiagnosticsArtifact& candidate_artifact,
                                      const nk::TraceCapture& candidate_trace,
                                      const BaselinePaths& baselines) {
    const auto baseline_artifact =
        nk::load_frame_diagnostics_artifact_json_file(baselines.artifact.string());
    if (!baseline_artifact) {
        std::cerr << "failed to load stable artifact baseline: " << baseline_artifact.error()
                  << "\n";
        return false;
    }

    const auto baseline_trace =
        nk::load_frame_diagnostics_trace_json_file(baselines.trace.string());
    if (!baseline_trace) {
        std::cerr << "failed to load stable trace baseline: " << baseline_trace.error() << "\n";
        return false;
    }

    const auto scenario_label = scenario_name(scenario);
    auto thresholds = StableScenarioThresholds{};
    if (scenario == Scenario::LocalizedRedraw && backend == nk::RendererBackend::Metal) {
        thresholds.max_artifact_total_ms_regression = 3.0;
        thresholds.max_trace_frame_ms_regression = 3.0;
    }
    const auto baseline_artifact_summary = summarize_artifact(*baseline_artifact);
    const auto candidate_artifact_summary = summarize_artifact(candidate_artifact);
    const auto baseline_trace_summary = summarize_trace(*baseline_trace);
    const auto candidate_trace_summary = summarize_trace(candidate_trace);

    bool failed = false;
    if (baseline_artifact_summary.frame_count != candidate_artifact_summary.frame_count) {
        std::cerr << "stable scenario regression [" << scenario_label
                  << "]: frame count baseline=" << baseline_artifact_summary.frame_count
                  << " candidate=" << candidate_artifact_summary.frame_count << "\n";
        failed = true;
    }

    failed |= check_regression("artifact max total ms",
                               baseline_artifact_summary.max_total_ms,
                               candidate_artifact_summary.max_total_ms,
                               thresholds.max_artifact_total_ms_regression,
                               scenario_label);
    failed |= check_regression("artifact max render nodes",
                               baseline_artifact_summary.max_render_node_count,
                               candidate_artifact_summary.max_render_node_count,
                               thresholds.max_render_node_regression,
                               scenario_label);
    failed |= check_regression("artifact max text shapes",
                               baseline_artifact_summary.max_text_shape_count,
                               candidate_artifact_summary.max_text_shape_count,
                               thresholds.max_text_shape_regression,
                               scenario_label);
    failed |= check_regression("artifact max image uploads",
                               baseline_artifact_summary.max_image_texture_upload_count,
                               candidate_artifact_summary.max_image_texture_upload_count,
                               thresholds.max_image_upload_regression,
                               scenario_label);
    failed |= check_regression("artifact max model materialize",
                               baseline_artifact_summary.max_model_row_materialize_count,
                               candidate_artifact_summary.max_model_row_materialize_count,
                               thresholds.max_model_materialize_regression,
                               scenario_label);

    failed |= check_regression("trace max frame ms",
                               baseline_trace_summary.max_frame_ms,
                               candidate_trace_summary.max_frame_ms,
                               thresholds.max_trace_frame_ms_regression,
                               scenario_label);
    failed |= check_regression("trace over-budget frames",
                               baseline_trace_summary.over_budget_frame_count,
                               candidate_trace_summary.over_budget_frame_count,
                               thresholds.max_over_budget_frame_regression,
                               scenario_label);
    failed |= check_regression("trace slow frames",
                               baseline_trace_summary.slow_frame_count,
                               candidate_trace_summary.slow_frame_count,
                               thresholds.max_slow_frame_regression,
                               scenario_label);
    failed |= check_regression("trace very slow frames",
                               baseline_trace_summary.very_slow_frame_count,
                               candidate_trace_summary.very_slow_frame_count,
                               thresholds.max_very_slow_frame_regression,
                               scenario_label);
    failed |= check_regression("trace posted tasks",
                               baseline_trace_summary.posted_task_count,
                               candidate_trace_summary.posted_task_count,
                               thresholds.max_posted_task_regression,
                               scenario_label);
    failed |= check_regression("trace timeouts",
                               baseline_trace_summary.timeout_count,
                               candidate_trace_summary.timeout_count,
                               thresholds.max_timeout_regression,
                               scenario_label);
    failed |= check_regression("trace intervals",
                               baseline_trace_summary.interval_count,
                               candidate_trace_summary.interval_count,
                               thresholds.max_interval_regression,
                               scenario_label);
    failed |= check_regression("trace idles",
                               baseline_trace_summary.idle_count,
                               candidate_trace_summary.idle_count,
                               thresholds.max_idle_regression,
                               scenario_label);
    failed |= check_regression("trace unlabeled runtime events",
                               baseline_trace_summary.unlabeled_runtime_event_count,
                               candidate_trace_summary.unlabeled_runtime_event_count,
                               thresholds.max_unlabeled_runtime_regression,
                               scenario_label);

    return !failed;
}

void schedule_mixed_workload(nk::Application& app, nk::Window& window, HarnessScene& scene) {
    app.event_loop().post(
        [&] {
            scene.title->set_text("Trace capture phase 1");
            scene.text_field->set_text("alpha beta gamma");
            window.request_frame();
        },
        "harness.mixed.text-phase");

    (void)app.event_loop().set_timeout(
        std::chrono::milliseconds{0},
        [&] {
            scene.model->append("Echo");
            scene.selection->set_current_row(scene.model->row_count() - 1);
            scene.selection->select(scene.model->row_count() - 1);
            window.request_frame();
        },
        "harness.mixed.model-phase");

    (void)app.event_loop().set_timeout(
        std::chrono::milliseconds{1},
        [&] {
            auto updated_pixels = build_pattern(96, 72, 1);
            scene.image_view->update_pixel_buffer(updated_pixels.data(), 96, 72);
            window.request_frame();
        },
        "harness.mixed.image-phase");

    (void)app.event_loop().add_idle(
        [&] {
            scene.title->set_text("Trace capture settled");
            scene.text_field->set_text("stable capture payload");
            scene.image_view->set_scale_mode(nk::ScaleMode::Bilinear);
            window.request_frame();
        },
        "harness.mixed.idle-phase");
}

void schedule_text_heavy_workload(nk::Application& app, nk::Window& window, HarnessScene& scene) {
    app.event_loop().post(
        [&] {
            scene.title->set_text("Text-heavy benchmark");
            scene.text_field->set_text("first wave: shaping and measuring multiple labels");
            for (std::size_t index = 0; index < scene.labels.size(); ++index) {
                scene.labels[index]->set_text("Paragraph " + std::to_string(index) +
                                              ": alpha beta gamma delta epsilon zeta eta theta.");
            }
            window.request_frame();
        },
        "harness.text-heavy.post");

    (void)app.event_loop().set_timeout(
        std::chrono::milliseconds{0},
        [&] {
            for (std::size_t index = 0; index < scene.labels.size(); ++index) {
                if ((index % 2U) == 0U) {
                    scene.labels[index]->set_text("Reflow " + std::to_string(index) +
                                                  ": stable strings still need a text pass.");
                }
            }
            scene.text_field->set_text("second wave: updated text payload for measurement churn");
            window.request_frame();
        },
        "harness.text-heavy.timeout");

    (void)app.event_loop().set_timeout(
        std::chrono::milliseconds{1},
        [&] {
            scene.title->set_text("Text-heavy settled");
            window.request_frame();
        },
        "harness.text-heavy.interval");

    (void)app.event_loop().add_idle(
        [&] {
            scene.text_field->set_text("final text-heavy payload");
            if (!scene.labels.empty()) {
                scene.labels.back()->set_text("Terminal line: deterministic final content.");
            }
            window.request_frame();
        },
        "harness.text-heavy.idle");
}

void schedule_list_heavy_workload(nk::Application& app, nk::Window& window, HarnessScene& scene) {
    app.event_loop().post(
        [&] {
            scene.title->set_text("List-heavy benchmark");
            scene.text_field->set_text("list model churn");
            for (int index = 0; index < 8; ++index) {
                scene.model->append("Appended row " + std::to_string(index));
            }
            window.request_frame();
        },
        "harness.list-heavy.post");

    (void)app.event_loop().set_timeout(
        std::chrono::milliseconds{0},
        [&] {
            scene.model->insert(1, "Inserted row");
            scene.model->remove(3);
            scene.selection->set_current_row(6);
            scene.selection->select(6);
            window.request_frame();
        },
        "harness.list-heavy.timeout");

    (void)app.event_loop().set_timeout(
        std::chrono::milliseconds{1},
        [&] {
            scene.model->clear();
            for (int index = 0; index < 20; ++index) {
                scene.model->append("Reloaded row " + std::to_string(index));
            }
            scene.selection->set_current_row(4);
            scene.selection->select(4);
            window.request_frame();
        },
        "harness.list-heavy.interval");

    (void)app.event_loop().add_idle(
        [&] {
            scene.title->set_text("List-heavy settled");
            window.request_frame();
        },
        "harness.list-heavy.idle");
}

void schedule_animation_heavy_workload(nk::Application& app,
                                       nk::Window& window,
                                       HarnessScene& scene) {
    int frame_index = 0;
    nk::CallbackHandle interval_handle{};

    app.event_loop().post(
        [&] {
            scene.title->set_text("Animation-heavy benchmark");
            scene.text_field->set_text("animation frame 0");
            window.request_frame();
        },
        "harness.animation-heavy.post");

    interval_handle = app.event_loop().set_interval(
        std::chrono::milliseconds{1},
        [&] {
            ++frame_index;
            auto updated_pixels = build_pattern(96, 72, frame_index + 1);
            scene.image_view->update_pixel_buffer(updated_pixels.data(), 96, 72);
            scene.text_field->set_text("animation frame " + std::to_string(frame_index));
            scene.image_view->set_scale_mode(
                (frame_index % 2) == 0 ? nk::ScaleMode::Bilinear : nk::ScaleMode::NearestNeighbor);
            window.request_frame();
            if (frame_index >= 4) {
                app.event_loop().cancel(interval_handle);
            }
        },
        "harness.animation-heavy.interval");

    (void)app.event_loop().add_idle(
        [&] {
            scene.title->set_text("Animation-heavy settled");
            window.request_frame();
        },
        "harness.animation-heavy.idle");
}

void schedule_localized_redraw_workload(nk::Application& app,
                                        nk::Window& window,
                                        HarnessScene& scene) {
    app.event_loop().post(
        [&] {
            scene.title->set_text("Localized redraw benchmark");
            scene.text_field->set_text("left image mutates; right image should stay stable");
            window.request_frame();
        },
        "harness.localized-redraw.post");

    (void)app.event_loop().set_timeout(
        std::chrono::milliseconds{0},
        [&] {
            auto updated_pixels = build_pattern(96, 72, 19);
            scene.image_view->update_pixel_buffer(updated_pixels.data(), 96, 72);
            window.request_frame();
        },
        "harness.localized-redraw.timeout");

    (void)app.event_loop().add_idle(
        [&] {
            scene.title->set_text("Localized redraw settled");
            window.request_frame();
        },
        "harness.localized-redraw.idle");
}

void schedule_workload(Scenario scenario,
                       nk::Application& app,
                       nk::Window& window,
                       HarnessScene& scene) {
    switch (scenario) {
    case Scenario::Mixed:
        schedule_mixed_workload(app, window, scene);
        break;
    case Scenario::TextHeavy:
        schedule_text_heavy_workload(app, window, scene);
        break;
    case Scenario::ListHeavy:
        schedule_list_heavy_workload(app, window, scene);
        break;
    case Scenario::AnimationHeavy:
        schedule_animation_heavy_workload(app, window, scene);
        break;
    case Scenario::LocalizedRedraw:
        schedule_localized_redraw_workload(app, window, scene);
        break;
    }
}

bool validate_trace_labels(Scenario scenario, std::span<const nk::TraceEvent> events) {
    switch (scenario) {
    case Scenario::Mixed:
        return trace_has_source(events, "harness.mixed.text-phase") &&
               trace_has_source(events, "harness.mixed.model-phase") &&
               trace_has_source(events, "harness.mixed.image-phase") &&
               trace_has_source(events, "harness.mixed.idle-phase");
    case Scenario::TextHeavy:
        return trace_has_source(events, "harness.text-heavy.post") &&
               trace_has_source(events, "harness.text-heavy.timeout") &&
               trace_has_source(events, "harness.text-heavy.interval") &&
               trace_has_source(events, "harness.text-heavy.idle");
    case Scenario::ListHeavy:
        return trace_has_source(events, "harness.list-heavy.post") &&
               trace_has_source(events, "harness.list-heavy.timeout") &&
               trace_has_source(events, "harness.list-heavy.interval") &&
               trace_has_source(events, "harness.list-heavy.idle");
    case Scenario::AnimationHeavy:
        return trace_has_source(events, "harness.animation-heavy.post") &&
               (trace_source_count(events, "harness.animation-heavy.interval") >= 4U);
    case Scenario::LocalizedRedraw:
        return trace_has_source(events, "harness.localized-redraw.post") &&
               trace_has_source(events, "harness.localized-redraw.timeout") &&
               trace_has_source(events, "harness.localized-redraw.idle");
    }
    return false;
}

bool validate_scenario_counters(Scenario scenario, const nk::FrameDiagnosticsArtifact& artifact) {
    const auto frames = std::span<const nk::FrameDiagnostics>(artifact.frames);
    const auto total_text_measure =
        sum_frame_counter(frames, [](const nk::FrameDiagnostics& frame) {
            return frame.widget_hotspot_totals.text_measure_count;
        });
    const auto total_text_shapes = sum_frame_counter(frames, [](const nk::FrameDiagnostics& frame) {
        return frame.render_hotspot_counters.text_shape_count;
    });
    const auto total_model_syncs = sum_frame_counter(frames, [](const nk::FrameDiagnostics& frame) {
        return frame.widget_hotspot_totals.model_sync_count;
    });
    const auto total_image_snapshots =
        sum_frame_counter(frames, [](const nk::FrameDiagnostics& frame) {
            return frame.widget_hotspot_totals.image_snapshot_count;
        });
    const auto total_image_nodes = sum_frame_counter(frames, [](const nk::FrameDiagnostics& frame) {
        return frame.render_hotspot_counters.image_node_count;
    });
    switch (scenario) {
    case Scenario::Mixed:
        return total_text_measure > 0 && total_model_syncs > 0 && total_image_snapshots > 0;
    case Scenario::TextHeavy:
        return total_text_measure >= 10 && total_text_shapes >= 10;
    case Scenario::ListHeavy:
        return total_model_syncs > 0;
    case Scenario::AnimationHeavy:
        return total_image_snapshots > 0 && total_image_nodes > 0;
    case Scenario::LocalizedRedraw:
        return total_image_snapshots > 0 && total_image_nodes > 0;
    }
    return false;
}

int poll_steps_for(Scenario scenario) {
    switch (scenario) {
    case Scenario::Mixed:
    case Scenario::TextHeavy:
    case Scenario::ListHeavy:
        return 10;
    case Scenario::AnimationHeavy:
        return 14;
    case Scenario::LocalizedRedraw:
        return 8;
    }
    return 10;
}

bool validate_backend_expectations(nk::RendererBackend backend,
                                   Scenario scenario,
                                   std::span<const nk::FrameDiagnostics> frames) {
    const auto has_localized_replay_reduction = [&frames]() {
        std::size_t full_replayed_command_count = 0;
        for (const auto& frame : frames) {
            if (frame.render_hotspot_counters.damage_region_count == 0) {
                full_replayed_command_count = std::max(
                    full_replayed_command_count,
                    frame.render_hotspot_counters.gpu_replayed_command_count);
            }
        }
        if (full_replayed_command_count == 0) {
            return false;
        }
        return std::any_of(frames.begin(), frames.end(), [&](const auto& frame) {
            return frame.render_hotspot_counters.damage_region_count > 0 &&
                   frame.render_hotspot_counters.gpu_replayed_command_count > 0 &&
                   frame.render_hotspot_counters.gpu_replayed_command_count <
                       full_replayed_command_count &&
                   frame.render_hotspot_counters.gpu_skipped_command_count > 0;
        });
    };

    switch (backend) {
    case nk::RendererBackend::Software:
        if (scenario == Scenario::LocalizedRedraw) {
            return std::any_of(frames.begin(), frames.end(), [](const auto& frame) {
                return frame.render_hotspot_counters.damage_region_count > 0 &&
                       frame.render_hotspot_counters.gpu_present_region_count > 0 &&
                       frame.render_hotspot_counters.gpu_present_path ==
                           nk::GpuPresentPath::SoftwareUpload;
            });
        }
        return true;
    case nk::RendererBackend::D3D11:
        if (scenario == Scenario::LocalizedRedraw) {
            return has_localized_replay_reduction() &&
                   std::any_of(frames.begin(), frames.end(), [](const auto& frame) {
                return frame.render_hotspot_counters.damage_region_count > 0 &&
                       frame.render_hotspot_counters.gpu_present_region_count > 0 &&
                       frame.render_hotspot_counters.gpu_present_path ==
                           nk::GpuPresentPath::PartialRedrawCopy &&
                       frame.render_hotspot_counters.gpu_present_tradeoff ==
                           nk::GpuPresentTradeoff::BandwidthFavored;
            });
        }
        return true;
    case nk::RendererBackend::Metal:
        if (scenario == Scenario::LocalizedRedraw) {
            return std::any_of(frames.begin(), frames.end(), [](const auto& frame) {
                return frame.render_hotspot_counters.damage_region_count > 0 &&
                       frame.render_hotspot_counters.gpu_present_region_count > 0 &&
                       frame.render_hotspot_counters.gpu_draw_call_count > 0 &&
                       frame.render_hotspot_counters.gpu_present_path ==
                           nk::GpuPresentPath::PartialRedrawCopy &&
                       frame.render_hotspot_counters.gpu_present_tradeoff ==
                           nk::GpuPresentTradeoff::DrawFavored;
            });
        }
        return true;
    case nk::RendererBackend::OpenGL:
        return true;
    case nk::RendererBackend::Vulkan:
        if (scenario == Scenario::LocalizedRedraw) {
            return has_localized_replay_reduction() &&
                   std::any_of(frames.begin(), frames.end(), [](const auto& frame) {
                return frame.render_hotspot_counters.damage_region_count > 0 &&
                       frame.render_hotspot_counters.gpu_present_path ==
                           nk::GpuPresentPath::PartialRedrawCopy &&
                       frame.render_hotspot_counters.gpu_present_tradeoff ==
                           nk::GpuPresentTradeoff::DrawFavored;
            });
        }
        return true;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        return usage(argc > 0 ? argv[0] : "nk_trace_capture_harness");
    }

    const auto scenario = parse_scenario(argv[1]);
    if (!scenario) {
        std::cerr << scenario.error() << "\n";
        return 2;
    }

    const auto artifact_path = std::filesystem::path(argv[2]);
    const auto trace_path = std::filesystem::path(argv[3]);
    HarnessOptions options;
    for (int index = 4; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg.starts_with("--backend=")) {
            options.backend = std::string(arg.substr(std::string_view("--backend=").size()));
            continue;
        }
        if (arg.starts_with("--baseline-artifact=")) {
            if (!options.baselines.has_value()) {
                options.baselines = BaselinePaths{};
            }
            options.baselines->artifact = std::filesystem::path(
                std::string(arg.substr(std::string_view("--baseline-artifact=").size())));
            continue;
        }
        if (arg.starts_with("--baseline-trace=")) {
            if (!options.baselines.has_value()) {
                options.baselines = BaselinePaths{};
            }
            options.baselines->trace = std::filesystem::path(
                std::string(arg.substr(std::string_view("--baseline-trace=").size())));
            continue;
        }
        std::cerr << "unknown argument: " << arg << "\n";
        return 2;
    }
    if (options.baselines.has_value() &&
        (options.baselines->artifact.empty() || options.baselines->trace.empty())) {
        std::cerr << "both --baseline-artifact and --baseline-trace are required together\n";
        return 2;
    }

    set_renderer_backend_env(options.backend);

    nk::Application app({
        .app_id = "org.nodalkit.trace_capture_harness." + scenario_name(*scenario),
        .app_name = "NodalKit Trace Capture Harness",
    });
    app.set_system_preferences_observation_enabled(false);

    nk::Window window({
        .title = "Trace Capture Harness",
        .width = 760,
        .height = 560,
    });

    auto scene = build_scene(*scenario);
    window.set_child(scene.root);
    window.present();
    if (!app.event_loop().poll()) {
        std::cerr << "initial poll did not process the first frame\n";
        return 1;
    }
    const auto actual_backend = window.renderer_backend();
    if (nk::renderer_backend_name(actual_backend) != options.backend) {
        std::cerr << "requested backend " << options.backend << " but got "
                  << nk::renderer_backend_name(actual_backend) << "\n";
        return 1;
    }

    app.event_loop().clear_debug_trace_events();
    schedule_workload(*scenario, app, window, scene);
    window.request_frame();

    for (int step = 0; step < poll_steps_for(*scenario); ++step) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        (void)app.event_loop().poll();
    }

    const auto history = window.debug_frame_history();
    const auto trace_events = app.event_loop().debug_trace_events();
    if (history.empty()) {
        std::cerr << "expected at least one captured frame for scenario "
                  << scenario_name(*scenario) << ", got " << history.size() << "\n";
        return 1;
    }
    if (trace_events.empty()) {
        std::cerr << "expected live runtime trace events for scenario " << scenario_name(*scenario)
                  << "\n";
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

    if (trace->events.empty()) {
        std::cerr << "expected saved trace events\n";
        return 1;
    }

    if (!validate_trace_labels(*scenario, trace_events)) {
        std::cerr << "trace capture is missing expected labeled harness events for scenario "
                  << scenario_name(*scenario) << "\n";
        return 1;
    }

    if (!validate_scenario_counters(*scenario, *artifact)) {
        std::cerr << "scenario counters did not match expectations for " << scenario_name(*scenario)
                  << "\n";
        return 1;
    }
    if (!validate_backend_expectations(actual_backend, *scenario, artifact->frames)) {
        std::cerr << "backend-specific expectations did not match for backend "
                  << nk::renderer_backend_name(actual_backend) << " in scenario "
                  << scenario_name(*scenario) << "\n";
        return 1;
    }

    const auto histogram = nk::build_frame_time_histogram(artifact->frames);
    if (histogram.total_count() != artifact->frames.size()) {
        std::cerr << "frame histogram did not match artifact frame count\n";
        return 1;
    }

    if (options.baselines.has_value() &&
        !validate_against_stable_baseline(
            *scenario, actual_backend, *artifact, *trace, *options.baselines)) {
        return 1;
    }

    return 0;
}
