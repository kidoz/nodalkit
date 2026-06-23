#include <nk/platform/drag_drop.h>
#include <utility>

namespace nk {

DragPayload DragPayload::from_text(std::string text, std::string mime_type) {
    return DragPayload{
        .mime_type = std::move(mime_type),
        .text = std::move(text),
    };
}

DragPayload DragPayload::from_files(std::vector<std::filesystem::path> files) {
    return DragPayload{
        .mime_type = "text/uri-list",
        .files = std::move(files),
    };
}

DragPayload DragPayload::from_application_data(std::string mime_type, std::any data) {
    return DragPayload{
        .mime_type = std::move(mime_type),
        .application_data = std::move(data),
    };
}

bool DragPayload::has_text() const {
    return !text.empty();
}

bool DragPayload::has_files() const {
    return !files.empty();
}

bool DragPayload::has_application_data() const {
    return application_data.has_value();
}

void DragDropEvent::accept(DragOperation operation) {
    accepted_operation = operation;
}

void DragDropEvent::reject() {
    accepted_operation = DragOperation::None;
}

bool DragDropEvent::is_accepted() const {
    return accepted_operation != DragOperation::None;
}

} // namespace nk
