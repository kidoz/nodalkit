#include "text_clipboard.h"

#include <nk/platform/application.h>
#include <utility>

namespace nk::detail {
namespace {
std::string& fallback(bool primary) {
    static std::string clipboard;
    static std::string selection;
    return primary ? selection : clipboard;
}
} // namespace

std::string read_text_clipboard(bool primary) {
    if (auto* app = Application::instance()) {
        return primary ? app->primary_selection_text() : app->clipboard_text();
    }
    return fallback(primary);
}

void write_text_clipboard(std::string text, bool primary) {
    if (auto* app = Application::instance()) {
        if (primary) {
            app->set_primary_selection_text(std::move(text));
        } else {
            app->set_clipboard_text(std::move(text));
        }
    } else {
        fallback(primary) = std::move(text);
    }
}
} // namespace nk::detail
