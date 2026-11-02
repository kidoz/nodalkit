#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <map>
#include <nk/foundation/result.h>
#include <nk/l10n/message_catalog.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace nk {

namespace {

using Messages = std::map<std::string, std::string, std::less<>>;

std::string_view trim(std::string_view text) {
    constexpr std::string_view kWhitespace = " \t";
    const auto first = text.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(kWhitespace);
    return text.substr(first, last - first + 1);
}

bool is_continuation(unsigned char byte) {
    return (byte & 0xC0U) == 0x80U;
}

bool is_valid_utf8(std::string_view text) {
    const auto byte = [&text](std::size_t index) {
        return static_cast<unsigned char>(text.at(index));
    };
    std::size_t i = 0;
    while (i < text.size()) {
        const auto first = byte(i);
        if (first <= 0x7FU) {
            ++i;
            continue;
        }

        std::size_t length = 0;
        if (first >= 0xC2U && first <= 0xDFU) {
            length = 2;
        } else if (first >= 0xE0U && first <= 0xEFU) {
            length = 3;
        } else if (first >= 0xF0U && first <= 0xF4U) {
            length = 4;
        } else {
            return false;
        }
        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t offset = 1; offset < length; ++offset) {
            if (!is_continuation(byte(i + offset))) {
                return false;
            }
        }

        if (length == 3) {
            const auto second = byte(i + 1);
            if ((first == 0xE0U && second < 0xA0U) || (first == 0xEDU && second > 0x9FU)) {
                return false;
            }
        } else if (length == 4) {
            const auto second = byte(i + 1);
            if ((first == 0xF0U && second < 0x90U) || (first == 0xF4U && second > 0x8FU)) {
                return false;
            }
        }
        i += length;
    }
    return true;
}

std::size_t find_unescaped_separator(std::string_view line) {
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (escaped) {
            escaped = false;
        } else if (line.at(i) == '\\') {
            escaped = true;
        } else if (line.at(i) == '=') {
            return i;
        }
    }
    return std::string_view::npos;
}

std::optional<std::string> decode_escapes(std::string_view text, char& invalid_escape) {
    std::string decoded;
    decoded.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.at(i) != '\\') {
            decoded.push_back(text.at(i));
            continue;
        }
        if (++i == text.size()) {
            invalid_escape = '\\';
            return std::nullopt;
        }
        switch (text.at(i)) {
        case '\\':
        case '=':
        case '#':
            decoded.push_back(text.at(i));
            break;
        case 'n':
            decoded.push_back('\n');
            break;
        case 'r':
            decoded.push_back('\r');
            break;
        case 't':
            decoded.push_back('\t');
            break;
        default:
            invalid_escape = text.at(i);
            return std::nullopt;
        }
    }
    return decoded;
}

Result<void>
parse_error(const std::filesystem::path& path, std::size_t line_number, std::string_view detail) {
    return Unexpected(std::string(path) + ":" + std::to_string(line_number) + ": " +
                      std::string(detail));
}

Result<void>
parse_catalog(std::string_view contents, const std::filesystem::path& path, Messages& messages) {
    constexpr std::string_view kBom = "\xEF\xBB\xBF";
    if (contents.starts_with(kBom)) {
        contents.remove_prefix(kBom.size());
    }
    if (!is_valid_utf8(contents)) {
        return Unexpected(std::string(path) + ": input is not valid UTF-8");
    }

    std::size_t line_number = 1;
    while (!contents.empty()) {
        const auto newline = contents.find('\n');
        auto line = contents.substr(0, newline);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        contents.remove_prefix(newline == std::string_view::npos ? contents.size() : newline + 1);

        const auto trimmed_line = trim(line);
        if (trimmed_line.empty() || trimmed_line.starts_with('#')) {
            ++line_number;
            continue;
        }

        const auto separator = find_unescaped_separator(line);
        if (separator == std::string_view::npos) {
            return parse_error(path, line_number, "expected message.id=translated text");
        }
        const auto raw_id = trim(line.substr(0, separator));
        if (raw_id.empty()) {
            return parse_error(path, line_number, "message ID is empty");
        }

        char invalid_escape = '\0';
        auto id = decode_escapes(raw_id, invalid_escape);
        if (!id) {
            return parse_error(
                path, line_number, std::string("invalid escape: \\") + invalid_escape);
        }
        if (id->empty()) {
            return parse_error(path, line_number, "message ID is empty");
        }
        auto value = decode_escapes(trim(line.substr(separator + 1)), invalid_escape);
        if (!value) {
            return parse_error(
                path, line_number, std::string("invalid escape: \\") + invalid_escape);
        }
        if (messages.contains(*id)) {
            return parse_error(path, line_number, "duplicate message ID: " + *id);
        }
        messages.emplace(std::move(*id), std::move(*value));
        ++line_number;
    }
    return {};
}

bool valid_argument_name(std::string_view name) {
    if (name.empty()) {
        return false;
    }
    for (const unsigned char ch : name) {
        const bool valid = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.';
        if (!valid) {
            return false;
        }
    }
    return true;
}

} // namespace

Result<void> MessageCatalog::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return Unexpected(std::string(path) + ": unable to open catalog");
    }
    const std::string contents{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
    if (input.bad()) {
        return Unexpected(std::string(path) + ": unable to read catalog");
    }

    Messages loaded;
    auto result = parse_catalog(contents, path, loaded);
    if (!result) {
        return result;
    }
    messages_.swap(loaded);
    return {};
}

void MessageCatalog::clear() noexcept {
    messages_.clear();
}

bool MessageCatalog::contains(std::string_view message_id) const {
    return messages_.contains(message_id);
}

std::size_t MessageCatalog::size() const noexcept {
    return messages_.size();
}

std::string MessageCatalog::translate(std::string_view message_id,
                                      const MessageArguments& arguments) const {
    const auto message = messages_.find(message_id);
    if (message == messages_.end()) {
        return std::string(message_id);
    }

    const std::string_view pattern = message->second;
    std::string translated;
    translated.reserve(pattern.size());
    std::size_t i = 0;
    while (i < pattern.size()) {
        if (pattern.substr(i).starts_with("{{")) {
            translated.push_back('{');
            i += 2;
            continue;
        }
        if (pattern.substr(i).starts_with("}}")) {
            translated.push_back('}');
            i += 2;
            continue;
        }
        if (pattern[i] != '{') {
            translated.push_back(pattern[i++]);
            continue;
        }

        const auto close = pattern.find('}', i + 1);
        if (close == std::string_view::npos) {
            translated.append(pattern.substr(i));
            break;
        }
        const auto name = pattern.substr(i + 1, close - i - 1);
        if (!valid_argument_name(name)) {
            translated.append(pattern.substr(i, close - i + 1));
            i = close + 1;
            continue;
        }
        const auto argument = arguments.find(name);
        if (argument == arguments.end()) {
            translated.append(pattern.substr(i, close - i + 1));
        } else {
            translated.append(argument->second);
        }
        i = close + 1;
    }
    return translated;
}

} // namespace nk
