#pragma once

/// @file file_dialog.h
/// @brief Public file-dialog capability and error types.

#include <nk/foundation/result.h>
#include <string>

namespace nk {

/// Reasons why an open-file dialog request did not produce a path.
enum class FileDialogError {
    Cancelled,   ///< The user dismissed the dialog without selecting a file.
    Unsupported, ///< The active platform backend does not implement the dialog.
    Unavailable, ///< No native platform backend is available.
    Failed,      ///< The dialog backend failed before returning a selection.
};

/// Result type returned by Application::open_file_dialog().
using OpenFileDialogResult = Result<std::string, FileDialogError>;

} // namespace nk
