#include "nk/style/gtk_palette.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nk/foundation/logging.h>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace nk {
namespace {

/// Parse a CSS color value into a `Color`. Handles `#rrggbb`, `#rgb`,
/// `#rrggbbaa`, `rgb(r,g,b)`, `rgba(r,g,b,a)` with channel values in 0-255.
/// Returns `std::nullopt` for values it cannot resolve (named colors, GTK
/// `alpha()`/`mix()` functions, `@reference`s), so the caller skips the slot.
std::optional<Color> parse_css_color(std::string_view value) {
    // Trim leading/trailing whitespace.
    auto start = value.find_first_not_of(" \t");
    auto end = value.find_last_not_of(" \t");
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    auto trimmed = value.substr(start, end - start + 1);

    // Hex form: #rgb, #rrggbb, #rrggbbaa.
    if (trimmed.starts_with('#')) {
        auto hex = trimmed.substr(1);
        auto parse_hex = [](std::string_view digits) -> std::optional<uint8_t> {
            unsigned int v = 0;
            auto [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), v, 16);
            if (ec != std::errc{} || ptr != digits.data() + digits.size()) {
                return std::nullopt;
            }
            return static_cast<uint8_t>(v);
        };
        if (hex.size() == 3) {
            auto r = parse_hex(std::string{hex[0], hex[0]});
            auto g = parse_hex(std::string{hex[1], hex[1]});
            auto b = parse_hex(std::string{hex[2], hex[2]});
            if (r && g && b) {
                return Color::from_rgb(*r, *g, *b);
            }
            return std::nullopt;
        }
        if (hex.size() == 6 || hex.size() == 8) {
            auto r = parse_hex(hex.substr(0, 2));
            auto g = parse_hex(hex.substr(2, 2));
            auto b = parse_hex(hex.substr(4, 2));
            if (!r || !g || !b) {
                return std::nullopt;
            }
            float a = 1.0F;
            if (hex.size() == 8) {
                auto av = parse_hex(hex.substr(6, 2));
                if (!av) {
                    return std::nullopt;
                }
                a = static_cast<float>(*av) / 255.0F;
            }
            return Color{*r / 255.0F, *g / 255.0F, *b / 255.0F, a};
        }
        return std::nullopt;
    }

    // rgb()/rgba() functional form.
    if (trimmed.starts_with("rgb(") || trimmed.starts_with("rgba(")) {
        auto open = trimmed.find('(');
        auto close = trimmed.find(')', open);
        if (open == std::string_view::npos || close == std::string_view::npos) {
            return std::nullopt;
        }
        auto args = trimmed.substr(open + 1, close - open - 1);
        std::array<int, 3> channels{};
        std::vector<std::string> tokens;
        std::istringstream ss{std::string(args)};
        std::string token;
        while (std::getline(ss, token, ',')) {
            token.erase(std::remove_if(token.begin(),
                                       token.end(),
                                       [](unsigned char c) { return std::isspace(c); }),
                        token.end());
            tokens.push_back(std::move(token));
        }
        const bool rgba = trimmed.starts_with("rgba(");
        if (tokens.size() != (rgba ? 4U : 3U)) {
            return std::nullopt;
        }
        for (std::size_t index = 0; index < channels.size(); ++index) {
            const auto& channel = tokens[index];
            const auto [ptr, error] =
                std::from_chars(channel.data(), channel.data() + channel.size(), channels[index]);
            if (error != std::errc{} || ptr != channel.data() + channel.size() ||
                channels[index] < 0 || channels[index] > 255) {
                return std::nullopt;
            }
        }
        float alpha = 1.0F;
        if (rgba) {
            const auto& alpha_token = tokens[3];
            const auto [ptr, error] =
                std::from_chars(alpha_token.data(), alpha_token.data() + alpha_token.size(), alpha);
            if (error != std::errc{} || ptr != alpha_token.data() + alpha_token.size() ||
                alpha < 0.0F || alpha > 1.0F) {
                return std::nullopt;
            }
        }
        return Color{
            static_cast<float>(channels[0]) / 255.0F,
            static_cast<float>(channels[1]) / 255.0F,
            static_cast<float>(channels[2]) / 255.0F,
            alpha,
        };
    }

    return std::nullopt;
}

/// Scan a stylesheet for `@define-color name value;` and collect the colors we
/// recognize into a map. Adwaita/libadwaita define these at the top of the CSS.
std::unordered_map<std::string, Color> extract_define_colors(const std::string& css) {
    static constexpr std::string_view prefix = "@define-color ";
    std::unordered_map<std::string, Color> result;

    std::size_t pos = 0;
    while ((pos = css.find(prefix, pos)) != std::string::npos) {
        auto name_start = pos + prefix.size();
        auto name_end = css.find_first_of(" \t", name_start);
        if (name_end == std::string::npos) {
            break;
        }
        auto value_start = css.find_first_not_of(" \t", name_end);
        auto value_end = css.find(';', value_start);
        if (value_end == std::string::npos) {
            break;
        }

        auto name = css.substr(name_start, name_end - name_start);
        auto value = std::string_view(css).substr(value_start, value_end - value_start);
        if (auto color = parse_css_color(value)) {
            result.insert_or_assign(name, *color);
        }
        pos = value_end + 1;
    }
    return result;
}

/// Read a file into a string. Returns an empty string on failure.
std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        return {};
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

/// Collect the combined CSS text for a theme. Searches the system theme
/// directory and the user config, reading both `gtk.css` and `gtk-dark.css` as
/// appropriate. Dark mode prefers `gtk-dark.css` but still pulls `gtk.css`
/// (which `@import`s the variant).
std::string collect_theme_css(std::string_view gtk_theme_name, bool dark) {
    std::string combined;

    const auto system_dir =
        std::filesystem::path("/usr/share/themes") / std::string(gtk_theme_name) / "gtk-4.0";
    const char* home = std::getenv("HOME");
    const auto user_dir =
        std::filesystem::path(home != nullptr ? home : ".") / ".config" / "gtk-4.0";

    // Later declarations override earlier ones, matching CSS cascade order.
    combined += read_file(system_dir / "gtk.css");
    if (dark) {
        combined += read_file(system_dir / "gtk-dark.css");
    }
    combined += read_file(user_dir / "gtk.css");
    if (dark) {
        combined += read_file(user_dir / "gtk-dark.css");
    }
    return combined;
}

} // namespace

std::optional<GtkPalette> parse_gtk_palette_css(std::string_view css) {
    auto colors = extract_define_colors(std::string(css));
    GtkPalette palette;
    if (auto it = colors.find("window_bg_color"); it != colors.end()) {
        palette.window_bg = it->second;
        palette.has_window_bg = true;
    }
    if (auto it = colors.find("window_fg_color"); it != colors.end()) {
        palette.window_fg = it->second;
        palette.has_window_fg = true;
    }
    if (auto it = colors.find("view_bg_color"); it != colors.end()) {
        palette.view_bg = it->second;
        palette.has_view_bg = true;
    }
    if (auto it = colors.find("view_fg_color"); it != colors.end()) {
        palette.view_fg = it->second;
        palette.has_view_fg = true;
    }
    if (auto it = colors.find("accent_bg_color"); it != colors.end()) {
        palette.accent_bg = it->second;
        palette.has_accent_bg = true;
    }
    if (auto it = colors.find("accent_fg_color"); it != colors.end()) {
        palette.accent_fg = it->second;
        palette.has_accent_fg = true;
    }
    if (auto it = colors.find("borders"); it != colors.end()) {
        palette.borders = it->second;
        palette.has_borders = true;
    }
    if (auto it = colors.find("dark_borders"); it != colors.end()) {
        palette.borders_dark = it->second;
        palette.has_borders_dark = true;
    }

    // If we didn't find any recognized colors, signal "no palette".
    if (!palette.has_window_bg && !palette.has_window_fg && !palette.has_view_bg &&
        !palette.has_accent_bg) {
        return std::nullopt;
    }
    return palette;
}

std::optional<GtkPalette> load_gtk_palette(std::string_view gtk_theme_name, bool dark) {
    if (gtk_theme_name.empty()) {
        return std::nullopt;
    }

    auto css = collect_theme_css(gtk_theme_name, dark);
    if (css.empty()) {
        NK_LOG_DEBUG("Theme", "No GTK stylesheet found for theme");
        return std::nullopt;
    }
    return parse_gtk_palette_css(css);
}

} // namespace nk
