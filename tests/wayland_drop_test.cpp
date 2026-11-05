#include "../src/platform/linux/wayland_drop.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Wayland URI lists decode escaped local files and ignore remote URLs",
          "[wayland][drag-drop]") {
    auto payload = nk::detail::decode_wayland_drop(
        "text/uri-list",
        "# Files\r\nfile:///tmp/game%20one.nes\r\nfile://localhost/tmp/disk.fds\n"
        "https://example.test/game.nes\nfile://remote/tmp/game.nes\n"
        "file:///tmp/bad%00.nes\nfile:///tmp/bad%Q0.nes\nfile:relative.nes\n");
    REQUIRE(payload.has_value());
    REQUIRE(payload->files.size() == 2);
    CHECK(payload->files[0] == std::filesystem::path("/tmp/game one.nes"));
    CHECK(payload->files[1] == std::filesystem::path("/tmp/disk.fds"));
    CHECK_FALSE(nk::detail::decode_wayland_drop("text/uri-list", "file://remote/rom.nes"));
    CHECK_FALSE(nk::detail::decode_wayland_drop("application/octet-stream", "binary"));
}

TEST_CASE("Wayland text drops preserve their offered text", "[wayland][drag-drop]") {
    for (const auto* mime : {"text/plain", "text/plain;charset=utf-8", "UTF8_STRING"}) {
        const auto payload = nk::detail::decode_wayland_drop(mime, "text\n\xC3\xA9");
        REQUIRE(payload.has_value());
        CHECK(payload->text == "text\n\xC3\xA9");
        CHECK(payload->mime_type == mime);
    }
}
