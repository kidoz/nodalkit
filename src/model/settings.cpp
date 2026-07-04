/// @file settings.cpp
/// @brief Implementation of the persistent Settings store.

#include <array>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <nk/model/settings.h>
#include <string>
#include <system_error>

namespace nk {

namespace {

constexpr std::array<char, 16> kHexDigits = {
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

// Percent-encode any byte present in @p reserved, plus '%' itself. Encoding is
// layered: the file layer and the list layer each escape their own structural
// characters (and '%'), so nested encoding round-trips cleanly.
std::string percent_encode(std::string_view text, std::string_view reserved) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        const bool is_reserved = ch == '%' || reserved.find(ch) != std::string_view::npos;
        if (is_reserved) {
            const auto byte = static_cast<unsigned char>(ch);
            out.push_back('%');
            out.push_back(kHexDigits[byte >> 4]);
            out.push_back(kHexDigits[byte & 0x0F]);
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

int hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

std::string percent_decode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int high = hex_value(text[i + 1]);
            const int low = hex_value(text[i + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

// File-structural characters: line and record separators.
constexpr std::string_view kFileReserved = "=\n\r";
// List-structural character: the element separator.
constexpr std::string_view kListReserved = ",";

std::vector<std::string> split_list(std::string_view raw) {
    std::vector<std::string> out;
    if (raw.empty()) {
        return out;
    }
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = raw.find(',', start);
        const std::string_view piece = raw.substr(
            start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
        out.push_back(percent_decode(piece));
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    return out;
}

std::string join_list(const std::vector<std::string>& values) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        out += percent_encode(values[i], kListReserved);
    }
    return out;
}

} // namespace

std::filesystem::path Settings::default_path(std::string_view app_name) {
    const std::string name(app_name);
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata != nullptr && *appdata != '\0') {
        return std::filesystem::path(appdata) / name / "settings.conf";
    }
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / "Library" / "Application Support" / name /
               "settings.conf";
    }
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::filesystem::path(xdg) / name / "settings.conf";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / name / "settings.conf";
    }
#endif
    return std::filesystem::path(name) / "settings.conf";
}

Settings::Settings(std::filesystem::path file) : file_(std::move(file)) {}

bool Settings::load() {
    values_.clear();
    std::ifstream in(file_, std::ios::binary);
    if (!in) {
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = percent_decode(std::string_view(line).substr(0, eq));
        std::string value = percent_decode(std::string_view(line).substr(eq + 1));
        values_.insert_or_assign(std::move(key), std::move(value));
    }
    return true;
}

bool Settings::save() const {
    std::error_code ec;
    const std::filesystem::path parent = file_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            return false;
        }
    }
    std::ofstream out(file_, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    for (const auto& [key, value] : values_) {
        out << percent_encode(key, kFileReserved) << '=' << percent_encode(value, kFileReserved)
            << '\n';
    }
    return out.good();
}

void Settings::set_string(std::string_view key, std::string_view value) {
    values_.insert_or_assign(std::string(key), std::string(value));
}

std::string Settings::get_string(std::string_view key, std::string_view fallback) const {
    const auto it = values_.find(key);
    return it != values_.end() ? it->second : std::string(fallback);
}

void Settings::set_int(std::string_view key, std::int64_t value) {
    values_.insert_or_assign(std::string(key), std::to_string(value));
}

std::int64_t Settings::get_int(std::string_view key, std::int64_t fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) {
        return fallback;
    }
    errno = 0;
    char* end = nullptr;
    const long long parsed = std::strtoll(it->second.c_str(), &end, 10);
    if (errno != 0 || end == it->second.c_str() || *end != '\0') {
        return fallback;
    }
    return static_cast<std::int64_t>(parsed);
}

void Settings::set_bool(std::string_view key, bool value) {
    values_.insert_or_assign(std::string(key), value ? "true" : "false");
}

bool Settings::get_bool(std::string_view key, bool fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return fallback;
    }
    if (it->second == "true" || it->second == "1") {
        return true;
    }
    if (it->second == "false" || it->second == "0") {
        return false;
    }
    return fallback;
}

void Settings::set_double(std::string_view key, double value) {
    // std::to_string fixes 6 fractional digits and loses precision. std::to_chars
    // emits the shortest decimal that round-trips back to the exact same double.
    std::array<char, 32> buffer{};
    const auto [ptr, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    std::string text = ec == std::errc() ? std::string(buffer.data(), ptr) : std::to_string(value);
    values_.insert_or_assign(std::string(key), std::move(text));
}

double Settings::get_double(std::string_view key, double fallback) const {
    const auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) {
        return fallback;
    }
    errno = 0;
    char* end = nullptr;
    const double parsed = std::strtod(it->second.c_str(), &end);
    if (errno != 0 || end == it->second.c_str() || *end != '\0') {
        return fallback;
    }
    return parsed;
}

void Settings::set_string_list(std::string_view key, const std::vector<std::string>& values) {
    values_.insert_or_assign(std::string(key), join_list(values));
}

std::vector<std::string> Settings::get_string_list(std::string_view key) const {
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return {};
    }
    return split_list(it->second);
}

void Settings::set_window_geometry(std::string_view key, const WindowGeometry& geometry) {
    set_string_list(key,
                    {std::to_string(geometry.x),
                     std::to_string(geometry.y),
                     std::to_string(geometry.width),
                     std::to_string(geometry.height)});
}

std::optional<WindowGeometry> Settings::get_window_geometry(std::string_view key) const {
    const std::vector<std::string> parts = get_string_list(key);
    if (parts.size() != 4) {
        return std::nullopt;
    }
    WindowGeometry geometry{};
    auto parse = [](const std::string& text, int& out) {
        errno = 0;
        char* end = nullptr;
        const long long value = std::strtoll(text.c_str(), &end, 10);
        if (errno != 0 || end == text.c_str() || *end != '\0') {
            return false;
        }
        out = static_cast<int>(value);
        return true;
    };
    if (!parse(parts[0], geometry.x) || !parse(parts[1], geometry.y) ||
        !parse(parts[2], geometry.width) || !parse(parts[3], geometry.height)) {
        return std::nullopt;
    }
    return geometry;
}

void Settings::push_recent_file(std::string_view path,
                                std::size_t max_entries,
                                std::string_view key) {
    std::vector<std::string> recents = get_string_list(key);
    const std::string entry(path);
    for (auto it = recents.begin(); it != recents.end();) {
        it = (*it == entry) ? recents.erase(it) : it + 1;
    }
    recents.insert(recents.begin(), entry);
    if (max_entries != 0 && recents.size() > max_entries) {
        recents.resize(max_entries);
    }
    set_string_list(key, recents);
}

std::vector<std::string> Settings::recent_files(std::string_view key) const {
    return get_string_list(key);
}

bool Settings::contains(std::string_view key) const {
    return values_.find(key) != values_.end();
}

void Settings::remove(std::string_view key) {
    const auto it = values_.find(key);
    if (it != values_.end()) {
        values_.erase(it);
    }
}

void Settings::clear() {
    values_.clear();
}

} // namespace nk
