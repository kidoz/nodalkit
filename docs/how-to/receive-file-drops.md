# Receive external files and text

On Linux Wayland, NodalKit receives local files offered as `text/uri-list` and
text offered as `text/plain;charset=utf-8`, `text/plain`, or `UTF8_STRING`.
External transfers request Copy. `Window::start_drag()` remains an in-process
source; it does not initiate an external Wayland drag.

Connect a handler on the receiving widget or its container:

```cpp
auto connection = root->on_drop().connect([](nk::DragDropEvent& event) {
    if (!event.payload || !event.payload->has_files()) {
        return;
    }
    for (const auto& path : event.payload->files) {
        // Validate application-specific extensions and open the file here.
    }
    event.accept(nk::DragOperation::Copy);
});
```

Enter and Motion carry the offered MIME type. File paths and text become
available in Drop after the asynchronous read completes. Cancelled, timed-out,
and malformed transfers do not deliver Drop. Reads are bounded to 16 MiB and a
two-second deadline; file lists are limited to 4096 entries. URI decoding accepts
absolute local paths, including `localhost`, and ignores remote file hosts and
non-file URLs. Receiving a URI never downloads a file.

The toolkit routes drops within the active modal. Applications must still
validate file types and decide whether replacing the current document or session
needs confirmation. Accept a drop only when the application has handled it.

The native receive/finish sequence follows the
[Wayland data-transfer protocol](https://wayland.freedesktop.org/docs/html/apa.html#protocol-spec-wl_data_offer).
