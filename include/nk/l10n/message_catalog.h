#pragma once

/// @file message_catalog.h
/// @brief Dependency-free translated-message catalogs and named placeholders.

#include <cstddef>
#include <filesystem>
#include <map>
#include <nk/foundation/result.h>
#include <string>
#include <string_view>

namespace nk {

/// Named UTF-8 values substituted into translated message templates.
using MessageArguments = std::map<std::string, std::string, std::less<>>;

/// A collection of translated UTF-8 messages loaded from a `.nkstrings` file.
///
/// Each non-comment line contains `message.id=translated text`. Message IDs and
/// surrounding value whitespace are trimmed. The parser supports `\\`, `\=`,
/// `\#`, `\n`, `\r`, and `\t` escapes. Lines whose first non-whitespace
/// character is `#` are comments. A UTF-8 BOM is accepted.
///
/// Templates substitute named `{argument}` placeholders. `{{` and `}}` produce
/// literal braces. Missing arguments remain as `{argument}` in the result;
/// argument values are inserted literally and are never expanded recursively.
/// This class deliberately provides no plural rules, locale negotiation, or
/// number/date formatting.
///
/// Not thread-safe: own and use a catalog on one thread. Applications choose
/// when to load or replace catalogs and update their widgets afterwards.
class MessageCatalog {
public:
    /// Load a catalog, replacing all current messages on success.
    /// On failure, returns a path/line diagnostic and preserves current messages.
    [[nodiscard]] Result<void> load(const std::filesystem::path& path);

    /// Remove every message.
    void clear() noexcept;

    /// Whether a message with this ID exists.
    [[nodiscard]] bool contains(std::string_view message_id) const;

    /// Number of messages in the catalog.
    [[nodiscard]] std::size_t size() const noexcept;

    /// Translate a message and substitute named arguments.
    /// Returns `message_id` unchanged when the message is not present.
    [[nodiscard]] std::string translate(std::string_view message_id,
                                        const MessageArguments& arguments = {}) const;

private:
    std::map<std::string, std::string, std::less<>> messages_;
};

} // namespace nk
