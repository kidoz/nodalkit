#include <algorithm>
#include <cctype>
#include <iostream>
#include <nk/debug/diagnostics.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct MatchEntry {
    std::string path;
    const nk::RenderSnapshotNode* node = nullptr;
};

struct Options {
    std::string_view snapshot_path;
    bool summary_only = false;
    std::string kind_filter;
    std::string source_widget_filter;
    std::string text_filter;
};

std::string lowercase(std::string_view text) {
    std::string value(text);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool contains_lowered(std::string_view haystack, std::string_view needle_lower) {
    if (needle_lower.empty()) {
        return true;
    }
    return lowercase(haystack).find(needle_lower) != std::string::npos;
}

std::string node_label(const nk::RenderSnapshotNode& node) {
    if (!node.source_widget_label.empty()) {
        return node.kind + "(" + node.source_widget_label + ")";
    }
    return node.kind;
}

bool node_matches(const nk::RenderSnapshotNode& node, const Options& options) {
    if (!options.kind_filter.empty() && !contains_lowered(node.kind, options.kind_filter)) {
        return false;
    }
    if (!options.source_widget_filter.empty() &&
        !contains_lowered(node.source_widget_label, options.source_widget_filter)) {
        return false;
    }
    if (!options.text_filter.empty() && !contains_lowered(node.detail, options.text_filter) &&
        !contains_lowered(node.kind, options.text_filter) &&
        !contains_lowered(node.source_widget_label, options.text_filter)) {
        return false;
    }
    return true;
}

void collect_matches(const nk::RenderSnapshotNode& node,
                     const Options& options,
                     std::vector<std::string>& path_parts,
                     std::vector<MatchEntry>& matches) {
    path_parts.push_back(node_label(node));
    if (node_matches(node, options)) {
        std::string path;
        for (std::size_t index = 0; index < path_parts.size(); ++index) {
            if (index > 0) {
                path += " > ";
            }
            path += path_parts[index];
        }
        matches.push_back({.path = std::move(path), .node = &node});
    }
    for (const auto& child : node.children) {
        collect_matches(child, options, path_parts, matches);
    }
    path_parts.pop_back();
}

int usage(const char* argv0) {
    std::cerr << "usage: " << (argv0 != nullptr ? argv0 : "nk_render_snapshot_view")
              << " <snapshot.json> [--summary-only] [--kind=<name>] [--source-widget=<label>]"
              << " [--find=<text>]\n";
    return 2;
}

bool parse_options(int argc, char** argv, Options& options) {
    if (argc < 2) {
        return false;
    }

    options.snapshot_path = argv[1];
    for (int index = 2; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg == "--summary-only") {
            options.summary_only = true;
            continue;
        }
        if (arg.starts_with("--kind=")) {
            options.kind_filter = lowercase(arg.substr(std::string_view("--kind=").size()));
            continue;
        }
        if (arg.starts_with("--source-widget=")) {
            options.source_widget_filter =
                lowercase(arg.substr(std::string_view("--source-widget=").size()));
            continue;
        }
        if (arg.starts_with("--find=")) {
            options.text_filter = lowercase(arg.substr(std::string_view("--find=").size()));
            continue;
        }
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        return usage(argc > 0 ? argv[0] : "nk_render_snapshot_view");
    }

    const auto snapshot = nk::load_render_snapshot_json_file(options.snapshot_path);
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
    if (!options.kind_filter.empty()) {
        std::cout << "filter kind: " << options.kind_filter << "\n";
    }
    if (!options.source_widget_filter.empty()) {
        std::cout << "filter source widget: " << options.source_widget_filter << "\n";
    }
    if (!options.text_filter.empty()) {
        std::cout << "filter text: " << options.text_filter << "\n";
    }

    if (options.summary_only) {
        return 0;
    }

    if (!options.kind_filter.empty() || !options.source_widget_filter.empty() ||
        !options.text_filter.empty()) {
        std::vector<std::string> path_parts;
        std::vector<MatchEntry> matches;
        collect_matches(*snapshot, options, path_parts, matches);
        std::cout << "matches: " << matches.size() << "\n";
        for (const auto& match : matches) {
            std::cout << "- " << match.path << "\n";
        }
        return 0;
    }

    std::cout << "tree:\n";
    std::cout << nk::format_render_snapshot_tree(*snapshot);
    if (!snapshot->children.empty()) {
        std::cout << "first child kind: " << snapshot->children.front().kind << "\n";
    }

    return 0;
}
