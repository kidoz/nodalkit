#include <algorithm>
#include <cctype>
#include <iostream>
#include <nk/accessibility/atspi_bridge.h>
#include <nk/debug/diagnostics.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::string_view artifact_path;
    bool summary_only = false;
    std::string find_filter;
    std::string role_filter;
    std::string action_filter;
};

std::string lowercase(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return result;
}

bool contains_lowered(std::string_view haystack, std::string_view needle_lower) {
    if (needle_lower.empty()) {
        return true;
    }
    return lowercase(haystack).find(needle_lower) != std::string::npos;
}

bool node_matches(const nk::AtspiAccessibleNode& node, const Options& options) {
    if (!options.role_filter.empty() && !contains_lowered(node.role_name, options.role_filter)) {
        return false;
    }
    if (!options.action_filter.empty()) {
        const auto it = std::find_if(
            node.action_names.begin(), node.action_names.end(), [&](std::string_view action_name) {
                return contains_lowered(action_name, options.action_filter);
            });
        if (it == node.action_names.end()) {
            return false;
        }
    }
    if (!options.find_filter.empty()) {
        if (contains_lowered(node.object_path, options.find_filter) ||
            contains_lowered(node.name, options.find_filter) ||
            contains_lowered(node.description, options.find_filter) ||
            contains_lowered(node.value, options.find_filter)) {
            return true;
        }
        for (const auto& relation : node.relations) {
            if (contains_lowered(relation, options.find_filter)) {
                return true;
            }
        }
        return false;
    }
    return true;
}

std::size_t count_action_nodes(const nk::AtspiAccessibleSnapshot& snapshot) {
    return static_cast<std::size_t>(std::count_if(
        snapshot.nodes.begin(), snapshot.nodes.end(), [](const nk::AtspiAccessibleNode& node) {
            return !node.action_names.empty();
        }));
}

std::size_t count_text_nodes(const nk::AtspiAccessibleSnapshot& snapshot) {
    return static_cast<std::size_t>(std::count_if(
        snapshot.nodes.begin(), snapshot.nodes.end(), [](const nk::AtspiAccessibleNode& node) {
            return !node.value.empty();
        }));
}

void print_node(const nk::AtspiAccessibleNode& node) {
    std::cout << "- " << node.object_path << "\n";
    std::cout << "  role: " << node.role_name << "\n";
    std::cout << "  name: " << (node.name.empty() ? "(none)" : node.name) << "\n";
    if (!node.description.empty()) {
        std::cout << "  description: " << node.description << "\n";
    }
    if (!node.value.empty()) {
        std::cout << "  value: " << node.value << "\n";
    }
    std::cout << "  bounds: " << node.bounds.width << "x" << node.bounds.height << " @ "
              << node.bounds.x << "," << node.bounds.y << "\n";
    std::cout << "  actions:";
    if (node.action_names.empty()) {
        std::cout << " (none)\n";
    } else {
        for (const auto& action : node.action_names) {
            std::cout << " " << action;
        }
        std::cout << "\n";
    }
    std::cout << "  interfaces:";
    for (const auto& interface_name : node.interfaces) {
        std::cout << " " << interface_name;
    }
    std::cout << "\n";
}

int usage(const char* argv0) {
    std::cerr << "usage: " << (argv0 != nullptr ? argv0 : "nk_atspi_snapshot_view")
              << " <widget_debug.json> [--summary-only] [--find=<needle>] [--role=<needle>]"
                 " [--action=<needle>]\n";
    return 2;
}

bool parse_options(int argc, char** argv, Options& options) {
    if (argc < 2) {
        return false;
    }
    options.artifact_path = argv[1];
    for (int index = 2; index < argc; ++index) {
        const std::string_view arg = argv[index];
        if (arg == "--summary-only") {
            options.summary_only = true;
            continue;
        }
        if (arg.starts_with("--find=")) {
            options.find_filter = lowercase(arg.substr(std::string_view("--find=").size()));
            continue;
        }
        if (arg.starts_with("--role=")) {
            options.role_filter = lowercase(arg.substr(std::string_view("--role=").size()));
            continue;
        }
        if (arg.starts_with("--action=")) {
            options.action_filter = lowercase(arg.substr(std::string_view("--action=").size()));
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
        return usage(argc > 0 ? argv[0] : "nk_atspi_snapshot_view");
    }

    const auto widget_root = nk::load_widget_debug_json_file(options.artifact_path);
    if (!widget_root) {
        std::cerr << widget_root.error() << "\n";
        return 1;
    }

    const auto snapshot = nk::build_atspi_window_snapshot(
        "/org/a11y/atspi/accessible/nodalkit",
        "fixture-window",
        widget_root->debug_name.empty() ? "NodalKit Fixture" : widget_root->debug_name,
        widget_root->allocation,
        *widget_root);

    std::cout << "nodes: " << snapshot.nodes.size() << "\n";
    std::cout << "action nodes: " << count_action_nodes(snapshot) << "\n";
    std::cout << "text nodes: " << count_text_nodes(snapshot) << "\n";
    std::cout << "window path: " << snapshot.nodes.front().object_path << "\n";

    if (options.summary_only && options.find_filter.empty() && options.role_filter.empty() &&
        options.action_filter.empty()) {
        return 0;
    }

    std::vector<const nk::AtspiAccessibleNode*> matches;
    for (const auto& node : snapshot.nodes) {
        if (node_matches(node, options)) {
            matches.push_back(&node);
        }
    }

    std::cout << "matches: " << matches.size() << "\n";
    for (const auto* node : matches) {
        print_node(*node);
    }

    return 0;
}
