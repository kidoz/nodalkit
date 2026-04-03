#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <nk/debug/diagnostics.h>
#include <sstream>
#include <string>
#include <string_view>

namespace {

struct BundleManifest {
    std::string format;
    std::string title;
    std::string renderer_backend;
    std::string widget_tree;
    std::string widget_tree_json;
    std::string frame_trace;
    std::string frame_diagnostics;
    std::string render_snapshot;
    std::string frame_summary;
    std::string selected_widget;
    std::string screenshot;
};

struct Options {
    std::string_view bundle_dir;
    bool summary_only = false;
};

int usage(const char* argv0) {
    std::cerr << "usage: " << (argv0 != nullptr ? argv0 : "nk_debug_bundle_view")
              << " <bundle-dir> [--summary-only]\n";
    return 2;
}

bool parse_options(int argc, char** argv, Options& options) {
    if (argc < 2 || argc > 3) {
        return false;
    }

    options.bundle_dir = argv[1];
    if (argc == 3) {
        if (std::string_view(argv[2]) != "--summary-only") {
            return false;
        }
        options.summary_only = true;
    }
    return true;
}

nk::Result<std::string> load_text_file(std::string_view path) {
    std::ifstream input(std::string(path), std::ios::binary);
    if (!input.is_open()) {
        return nk::Unexpected(std::string("could not open file"));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

nk::Result<std::string> extract_json_string(std::string_view json, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto key_pos = json.find(needle);
    if (key_pos == std::string_view::npos) {
        return nk::Unexpected("missing key: " + std::string(key));
    }
    const auto colon_pos = json.find(':', key_pos + needle.size());
    if (colon_pos == std::string_view::npos) {
        return nk::Unexpected("missing ':' for key: " + std::string(key));
    }
    const auto first_quote = json.find('"', colon_pos + 1);
    if (first_quote == std::string_view::npos) {
        return nk::Unexpected("missing opening quote for key: " + std::string(key));
    }
    const auto second_quote = json.find('"', first_quote + 1);
    if (second_quote == std::string_view::npos) {
        return nk::Unexpected("missing closing quote for key: " + std::string(key));
    }
    return std::string(json.substr(first_quote + 1, second_quote - first_quote - 1));
}

nk::Result<BundleManifest> load_manifest(const std::filesystem::path& bundle_dir) {
    const auto manifest_path = bundle_dir / "manifest.json";
    const auto manifest_text = load_text_file(manifest_path.string());
    if (!manifest_text) {
        return nk::Unexpected("could not load manifest.json: " + manifest_text.error());
    }

    BundleManifest manifest;
    auto assign = [&](std::string& out, std::string_view key) -> bool {
        const auto value = extract_json_string(*manifest_text, key);
        if (!value) {
            return false;
        }
        out = *value;
        return true;
    };

    if (!assign(manifest.format, "format") || !assign(manifest.title, "title") ||
        !assign(manifest.renderer_backend, "renderer_backend") ||
        !assign(manifest.widget_tree, "widget_tree") ||
        !assign(manifest.widget_tree_json, "widget_tree_json") ||
        !assign(manifest.frame_trace, "frame_trace") ||
        !assign(manifest.frame_diagnostics, "frame_diagnostics") ||
        !assign(manifest.render_snapshot, "render_snapshot") ||
        !assign(manifest.frame_summary, "frame_summary") ||
        !assign(manifest.selected_widget, "selected_widget") ||
        !assign(manifest.screenshot, "screenshot")) {
        return nk::Unexpected(std::string("manifest.json is missing required fields"));
    }

    return manifest;
}

std::size_t count_widget_nodes(const nk::WidgetDebugNode& node) {
    std::size_t count = 1;
    for (const auto& child : node.children) {
        count += count_widget_nodes(child);
    }
    return count;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        return usage(argc > 0 ? argv[0] : "nk_debug_bundle_view");
    }

    const std::filesystem::path bundle_dir(options.bundle_dir);
    const auto manifest = load_manifest(bundle_dir);
    if (!manifest) {
        std::cerr << manifest.error() << "\n";
        return 1;
    }

    const auto widget_tree =
        nk::load_widget_debug_json_file((bundle_dir / manifest->widget_tree_json).string());
    if (!widget_tree) {
        std::cerr << widget_tree.error() << "\n";
        return 1;
    }

    const auto selected_widget =
        nk::load_widget_debug_json_file((bundle_dir / manifest->selected_widget).string());
    if (!selected_widget) {
        std::cerr << selected_widget.error() << "\n";
        return 1;
    }

    const auto trace =
        nk::load_frame_diagnostics_trace_json_file((bundle_dir / manifest->frame_trace).string());
    if (!trace) {
        std::cerr << trace.error() << "\n";
        return 1;
    }

    const auto frame_artifact = nk::load_frame_diagnostics_artifact_json_file(
        (bundle_dir / manifest->frame_diagnostics).string());
    if (!frame_artifact) {
        std::cerr << frame_artifact.error() << "\n";
        return 1;
    }

    const auto snapshot =
        nk::load_render_snapshot_json_file((bundle_dir / manifest->render_snapshot).string());
    if (!snapshot) {
        std::cerr << snapshot.error() << "\n";
        return 1;
    }

    const auto frame_histogram = nk::build_frame_time_histogram(frame_artifact->frames);
    double slowest_frame_ms = 0.0;
    for (const auto& frame : frame_artifact->frames) {
        slowest_frame_ms = std::max(slowest_frame_ms, frame.total_ms);
    }

    std::size_t runtime_event_count = 0;
    std::size_t frame_event_count = 0;
    for (const auto& event : trace->events) {
        if (event.category == "frame") {
            ++frame_event_count;
        } else {
            ++runtime_event_count;
        }
    }

    std::cout << "format: " << manifest->format << "\n";
    std::cout << "title: " << manifest->title << "\n";
    std::cout << "renderer backend: " << manifest->renderer_backend << "\n";
    std::cout << "bundle dir: " << bundle_dir.string() << "\n";
    std::cout << "widget nodes: " << count_widget_nodes(*widget_tree) << "\n";
    std::cout << "selected widget: " << selected_widget->type_name;
    if (!selected_widget->debug_name.empty()) {
        std::cout << " (" << selected_widget->debug_name << ")";
    }
    std::cout << "\n";
    std::cout << "trace events: " << trace->events.size() << "\n";
    std::cout << "runtime events: " << runtime_event_count << "\n";
    std::cout << "frame events: " << frame_event_count << "\n";
    std::cout << "frames: " << frame_artifact->frames.size() << "\n";
    std::cout << "slowest frame ms: " << std::fixed << std::setprecision(2) << slowest_frame_ms
              << "\n";
    std::cout << "within budget: " << frame_histogram.within_budget_count << "\n";
    std::cout << "over budget: " << frame_histogram.over_budget_count << "\n";
    std::cout << "slow: " << frame_histogram.slow_count << "\n";
    std::cout << "very slow: " << frame_histogram.very_slow_count << "\n";
    std::cout << "render snapshot nodes: " << nk::count_render_snapshot_nodes(*snapshot) << "\n";
    std::cout << "render root kind: " << snapshot->kind << "\n";

    if (options.summary_only) {
        return 0;
    }

    std::string frame_summary;
    if (const auto loaded = load_text_file((bundle_dir / manifest->frame_summary).string());
        loaded) {
        frame_summary = *loaded;
    }
    std::cout << "files:\n";
    std::cout << "  widget_tree: " << manifest->widget_tree << "\n";
    std::cout << "  widget_tree_json: " << manifest->widget_tree_json << "\n";
    std::cout << "  frame_trace: " << manifest->frame_trace << "\n";
    std::cout << "  frame_diagnostics: " << manifest->frame_diagnostics << "\n";
    std::cout << "  render_snapshot: " << manifest->render_snapshot << "\n";
    std::cout << "  frame_summary: " << manifest->frame_summary << "\n";
    std::cout << "  selected_widget: " << manifest->selected_widget << "\n";
    std::cout << "  screenshot: " << manifest->screenshot << "\n";
    std::cout << "frame summary:\n" << frame_summary;

    return 0;
}
