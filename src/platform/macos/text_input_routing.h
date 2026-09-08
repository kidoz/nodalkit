#pragma once

#include <nk/platform/events.h>

namespace nk::detail {

// Ordinary marked-text keys belong to the input context so candidate navigation
// and confirmation do not trigger editor commands. App shortcuts remain direct.
inline bool
macos_dispatch_key_directly(nk::KeyCode key, nk::Modifiers modifiers, bool marked_text) {
    if ((modifiers & nk::Modifiers::Super) == nk::Modifiers::Super ||
        (modifiers & nk::Modifiers::Ctrl) == nk::Modifiers::Ctrl) {
        return true;
    }

    if (marked_text) {
        return false;
    }

    switch (key) {
    case nk::KeyCode::Unknown:
        return false;
    case nk::KeyCode::Return:
    case nk::KeyCode::Escape:
    case nk::KeyCode::Backspace:
    case nk::KeyCode::Delete:
    case nk::KeyCode::Tab:
    case nk::KeyCode::Home:
    case nk::KeyCode::End:
    case nk::KeyCode::PageUp:
    case nk::KeyCode::PageDown:
    case nk::KeyCode::Left:
    case nk::KeyCode::Right:
    case nk::KeyCode::Up:
    case nk::KeyCode::Down:
    case nk::KeyCode::F1:
    case nk::KeyCode::F2:
    case nk::KeyCode::F3:
    case nk::KeyCode::F4:
    case nk::KeyCode::F5:
    case nk::KeyCode::F6:
    case nk::KeyCode::F7:
    case nk::KeyCode::F8:
    case nk::KeyCode::F9:
    case nk::KeyCode::F10:
    case nk::KeyCode::F11:
    case nk::KeyCode::F12:
        return true;
    default:
        return false;
    }
}

} // namespace nk::detail
