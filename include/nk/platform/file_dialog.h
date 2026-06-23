#pragma once

/// @file file_dialog.h
/// @brief Public file-dialog capability and error types.

#include <nk/foundation/result.h>
#include <string>
#include <vector>

namespace nk {

/// Reasons why an open-file dialog request did not produce a path.
enum class FileDialogError {
    Cancelled,   ///< The user dismissed the dialog without selecting a file.
    Unsupported, ///< The active platform backend does not implement the dialog.
    Unavailable, ///< No native platform backend is available.
    Failed,      ///< The dialog backend failed before returning a selection.
};

/// Result type returned by file-dialog APIs.
using FileDialogResult = Result<std::string, FileDialogError>;

/// Result type returned by Application::open_file_dialog_async().
using OpenFileDialogResult = FileDialogResult;

/// Options for Application::save_file_dialog_async().
struct SaveFileDialogOptions {
    /// The title of the dialog.
    std::string title;
    /// Initial filename shown to the user.
    std::string suggested_filename;
    /// File extension or MIME filters.
    std::vector<std::string> filters;
    /// Ask before replacing an existing file when supported.
    bool confirm_overwrite = true;
};

/// Result type returned by Application::save_file_dialog_async().
using SaveFileDialogResult = FileDialogResult;

} // namespace nk
