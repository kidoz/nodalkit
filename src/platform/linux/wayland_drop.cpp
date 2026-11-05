#include "wayland_drop.h"

#include <cctype>
#include <cstddef>
#include <filesystem>
#include <nk/platform/drag_drop.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nk::detail {
namespace {

int hex_digit(char digit) {
    if (digit >= '0' && digit <= '9') {
        return digit - '0';
    }
    if (digit >= 'a' && digit <= 'f') {
        return digit - 'a' + 10;
    }
    if (digit >= 'A' && digit <= 'F') {
        return digit - 'A' + 10;
    }
    return -1;
}

std::optional<std::filesystem::path> local_file(std::string_view uri) {
    if (!uri.starts_with("file:")) {
        return std::nullopt;
    }
    uri.remove_prefix(5);
    if (uri.starts_with("//")) {
        uri.remove_prefix(2);
        const auto slash = uri.find('/');
        if (slash == std::string_view::npos) {
            return std::nullopt;
        }
        auto host = std::string(uri.substr(0, slash));
        for (char& ch : host) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        if (!host.empty() && host != "localhost") {
            return std::nullopt;
        }
        uri.remove_prefix(slash);
    }
    if (!uri.starts_with('/') || uri.find_first_of("?#") != std::string_view::npos) {
        return std::nullopt;
    }
    std::string path;
    for (std::size_t index = 0; index < uri.size(); ++index) {
        char ch = uri[index];
        if (ch == '%') {
            if (index + 2 >= uri.size()) {
                return std::nullopt;
            }
            const int high = hex_digit(uri[index + 1]);
            const int low = hex_digit(uri[index + 2]);
            if (high < 0 || low < 0) {
                return std::nullopt;
            }
            ch = static_cast<char>((high * 16) + low);
            index += 2;
        }
        if (ch == '\0') {
            return std::nullopt;
        }
        path.push_back(ch);
    }
    return std::filesystem::path(path);
}

} // namespace

std::optional<DragPayload> decode_wayland_drop(std::string_view mime, std::string data) {
    if (mime == "text/uri-list") {
        std::vector<std::filesystem::path> files;
        std::string_view remaining = data;
        while (!remaining.empty()) {
            const auto end = remaining.find('\n');
            auto line = remaining.substr(0, end);
            if (line.ends_with('\r')) {
                line.remove_suffix(1);
            }
            if (!line.empty() && !line.starts_with('#')) {
                if (auto file = local_file(line)) {
                    if (files.size() >= 4096) {
                        return std::nullopt;
                    }
                    files.push_back(std::move(*file));
                }
            }
            if (end == std::string_view::npos) {
                break;
            }
            remaining.remove_prefix(end + 1);
        }
        if (files.empty()) {
            return std::nullopt;
        }
        return DragPayload::from_files(std::move(files));
    }
    if (mime == "text/plain;charset=utf-8" || mime == "text/plain" || mime == "UTF8_STRING") {
        return DragPayload::from_text(std::move(data), std::string(mime));
    }
    return std::nullopt;
}

} // namespace nk::detail
