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
    const nk::WidgetDebugNode* node = nullptr;
};

struct Options {
    std::string_view artifact_path;
    bool summary_only = false;
    std::string filter;
    std::string state_filter;
};

std::string lowercase(std::string_view text) {
    std::string value(text);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool matches_state(const nk::WidgetDebugNode& node, std::string_view state_filter) {
    if (state_filter.empty()) {
        return true;
    }
    if (state_filter == "focused") {
        return node.focused;
    }
    if (state_filter == "hovered") {
        return node.hovered;
    }
    if (state_filter == "pressed") {
        return node.pressed;
    }
    if (state_filter == "focusable") {
        return node.focusable;
    }
    if (state_filter == "visible") {
        return node.visible;
    }
    if (state_filter == "sensitive") {
        return node.sensitive;
    }
    if (state_filter == "pending-redraw") {
        return node.pending_redraw;
    }
    if (state_filter == "pending-layout") {
        return node.pending_layout;
    }
    return false;
}

bool matches_text_filter(const nk::WidgetDebugNode& node, std::string_view needle_lower) {
    if (needle_lower.empty()) {
        return true;
    }
    if (lowercase(node.type_name).find(needle_lower) != std::string::npos) {
        return true;
    }
    if (lowercase(node.debug_name).find(needle_lower) != std::string::npos) {
        return true;
    }
    for (const auto& style_class : node.style_classes) {
        if (lowercase(style_class).find(needle_lower) != std::string::npos) {
            return true;
        }
    }
    return false;
}

bool node_matches(const nk::WidgetDebugNode& node, const Options& options) {
    return matches_text_filter(node, options.filter) && matches_state(node, options.state_filter);
}

std::size_t count_nodes(const nk::WidgetDebugNode& node) {
    std::size_t count = 1;
    for (const auto& child : node.children) {
        count += count_nodes(child);
    }
    return count;
}

std::size_t max_depth(const nk::WidgetDebugNode& node, std::size_t depth = 0) {
    std::size_t result = depth;
    for (const auto& child : node.children) {
        result = std::max(result, max_depth(child, depth + 1));
    }
    return result;
}

std::string node_label(const nk::WidgetDebugNode& node) {
    if (!node.debug_name.empty()) {
        return node.type_name + "(" + node.debug_name + ")";
    }
    return node.type_name;
}

void collect_matches(const nk::WidgetDebugNode& node,
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

void print_node_summary(const nk::WidgetDebugNode& node) {
    std::cout << "  type: " << node.type_name << "\n";
    std::cout << "  debug name: " << (node.debug_name.empty() ? "(none)" : node.debug_name) << "\n";
    std::cout << "  allocation: " << node.allocation.width << "x" << node.allocation.height << " @ "
              << node.allocation.x << "," << node.allocation.y << "\n";
    std::cout << "  visible: " << (node.visible ? "yes" : "no") << "\n";
    std::cout << "  sensitive: " << (node.sensitive ? "yes" : "no") << "\n";
    std::cout << "  focusable: " << (node.focusable ? "yes" : "no") << "\n";
    std::cout << "  style classes:";
    if (node.style_classes.empty()) {
        std::cout << " (none)\n";
    } else {
        for (const auto& style_class : node.style_classes) {
            std::cout << " ." << style_class;
        }
        std::cout << "\n";
    }
    std::cout << "  children: " << node.children.size() << "\n";
}

int usage(const char* argv0) {
    std::cerr << "usage: " << (argv0 != nullptr ? argv0 : "nk_widget_debug_view")
              << " <widget_debug.json> [--summary-only] [--find=<needle>] [--state=<name>]\n";
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
            options.filter = lowercase(arg.substr(std::string_view("--find=").size()));
            continue;
        }
        if (arg.starts_with("--state=")) {
            options.state_filter = lowercase(arg.substr(std::string_view("--state=").size()));
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
        return usage(argc > 0 ? argv[0] : "nk_widget_debug_view");
    }

    const auto root = nk::load_widget_debug_json_file(options.artifact_path);
    if (!root) {
        std::cerr << root.error() << "\n";
        return 1;
    }

    std::cout << "format: " << nk::widget_debug_artifact_format() << "\n";
    std::cout << "nodes: " << count_nodes(*root) << "\n";
    std::cout << "max depth: " << max_depth(*root) << "\n";
    std::cout << "root:\n";
    print_node_summary(*root);
    if (!options.filter.empty()) {
        std::cout << "filter text: " << options.filter << "\n";
    }
    if (!options.state_filter.empty()) {
        std::cout << "filter state: " << options.state_filter << "\n";
    }

    if (!options.filter.empty() || !options.state_filter.empty()) {
        std::vector<std::string> path_parts;
        std::vector<MatchEntry> matches;
        collect_matches(*root, options, path_parts, matches);
        std::cout << "matches: " << matches.size() << "\n";
        for (const auto& match : matches) {
            std::cout << "- " << match.path << "\n";
        }
        return 0;
    }

    if (options.summary_only) {
        return 0;
    }

    std::cout << "tree:\n";
    std::cout << nk::format_widget_debug_tree(*root);
    return 0;
}
