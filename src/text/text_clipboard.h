#pragma once

#include <functional>
#include <string>

namespace nk::detail {

// Use the application clipboard when available, otherwise the same headless
// fallback for every built-in editor. Primary selection is a separate channel.
std::string read_text_clipboard(bool primary = false);
void read_text_clipboard_async(std::function<void(std::string)> callback, bool primary = false);
void write_text_clipboard(std::string text, bool primary = false);

} // namespace nk::detail
