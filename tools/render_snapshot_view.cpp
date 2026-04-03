#include <iostream>
#include <nk/debug/diagnostics.h>
#include <string_view>

namespace {

int usage(const char* argv0) {
    std::cerr << "usage: " << (argv0 != nullptr ? argv0 : "nk_render_snapshot_view")
              << " <snapshot.json> [--summary-only]\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        return usage(argc > 0 ? argv[0] : "nk_render_snapshot_view");
    }

    const std::string_view snapshot_path = argv[1];
    const bool summary_only = argc == 3 && std::string_view(argv[2]) == "--summary-only";
    if (argc == 3 && !summary_only) {
        return usage(argv[0]);
    }

    const auto snapshot = nk::load_render_snapshot_json_file(snapshot_path);
    if (!snapshot) {
        std::cerr << snapshot.error() << "\n";
        return 1;
    }

    const auto node_count = nk::count_render_snapshot_nodes(*snapshot);
    std::cout << "format: " << nk::render_snapshot_artifact_format() << "\n";
    std::cout << "nodes: " << node_count << "\n";
    std::cout << "root kind: " << snapshot->kind << "\n";
    std::cout << "root detail: " << snapshot->detail << "\n";
    std::cout << "root bounds: " << snapshot->bounds.width << "x" << snapshot->bounds.height
              << " @ " << snapshot->bounds.x << "," << snapshot->bounds.y << "\n";
    if (!snapshot->source_widget_label.empty()) {
        std::cout << "source widget: " << snapshot->source_widget_label << "\n";
    }

    if (summary_only) {
        return 0;
    }

    std::cout << "tree:\n";
    std::cout << nk::format_render_snapshot_tree(*snapshot);
    if (!snapshot->children.empty()) {
        std::cout << "first child kind: " << snapshot->children.front().kind << "\n";
    }

    return 0;
}
