#pragma once

/// @file drag_drop.h
/// @brief Public drag-and-drop payload and event types.

#include <any>
#include <filesystem>
#include <memory>
#include <nk/actions/shortcut.h>
#include <nk/foundation/types.h>
#include <string>
#include <vector>

namespace nk {

/// Action requested or accepted for a drag-and-drop transfer.
enum class DragOperation {
    None,
    Copy,
    Move,
    Link,
};

/// Drag/drop lifecycle event delivered to widgets.
enum class DragDropEventType {
    Enter,
    Motion,
    Leave,
    Drop,
};

/// Data carried by a drag operation.
struct DragPayload {
    /// MIME-like type describing the payload. Application-internal payloads
    /// should use an application-owned type such as `application/x-example-row`.
    std::string mime_type;

    /// Text payload for text drops.
    std::string text;

    /// Files supplied by an external file drop.
    std::vector<std::filesystem::path> files;

    /// In-process application data. This is never portable across processes.
    std::any application_data;

    [[nodiscard]] static DragPayload from_text(std::string text,
                                               std::string mime_type = "text/plain;charset=utf-8");
    [[nodiscard]] static DragPayload from_files(std::vector<std::filesystem::path> files);
    [[nodiscard]] static DragPayload from_application_data(std::string mime_type, std::any data);

    [[nodiscard]] bool has_text() const;
    [[nodiscard]] bool has_files() const;
    [[nodiscard]] bool has_application_data() const;
};

/// Mutable drag/drop event. Drop targets accept by calling accept().
struct DragDropEvent {
    DragDropEventType type = DragDropEventType::Motion;
    Point position{};
    std::shared_ptr<const DragPayload> payload;
    DragOperation requested_operation = DragOperation::Copy;
    DragOperation accepted_operation = DragOperation::None;
    Modifiers modifiers = Modifiers::None;
    bool external = false;

    void accept(DragOperation operation = DragOperation::Copy);
    void reject();
    [[nodiscard]] bool is_accepted() const;
};

} // namespace nk
