/// @file wayland_portal_contract_test.cpp
/// @brief Pure tests for Wayland portal request/filter/result behavior.

#include "src/platform/linux/wayland_portal_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <nk/platform/file_dialog.h>

TEST_CASE("Wayland portal request paths sanitize the D-Bus unique name", "[wayland][portal]") {
    REQUIRE(nk::detail::sanitize_dbus_unique_name(":1.42") == "1_42");
    REQUIRE(nk::detail::portal_request_path(":1.42", "nk_123") ==
            "/org/freedesktop/portal/desktop/request/1_42/nk_123");
}

TEST_CASE("Wayland portal filters normalize MIME types and glob patterns", "[wayland][portal]") {
    REQUIRE(nk::detail::portal_filter_value("").has_value() == false);
    REQUIRE(nk::detail::portal_filter_value("text/plain") ==
            nk::detail::PortalFilterEntry{.kind = nk::detail::PortalFilterKind::MimeType,
                                          .value = "text/plain"});
    REQUIRE(nk::detail::portal_filter_value(".png") ==
            nk::detail::PortalFilterEntry{.kind = nk::detail::PortalFilterKind::GlobPattern,
                                          .value = "*.png"});
    REQUIRE(nk::detail::portal_filter_value("jpg") ==
            nk::detail::PortalFilterEntry{.kind = nk::detail::PortalFilterKind::GlobPattern,
                                          .value = "*.jpg"});
    REQUIRE(nk::detail::portal_filter_value("*.svg") ==
            nk::detail::PortalFilterEntry{.kind = nk::detail::PortalFilterKind::GlobPattern,
                                          .value = "*.svg"});
}

TEST_CASE("Wayland portal filter lists drop empty entries", "[wayland][portal]") {
    const std::vector<std::string> filters = {"", ".png", "image/jpeg"};
    const auto entries = nk::detail::portal_filter_entries(filters);

    REQUIRE(entries.size() == 2);
    REQUIRE(entries[0] == nk::detail::PortalFilterEntry{
                              .kind = nk::detail::PortalFilterKind::GlobPattern,
                              .value = "*.png",
                          });
    REQUIRE(entries[1] == nk::detail::PortalFilterEntry{
                              .kind = nk::detail::PortalFilterKind::MimeType,
                              .value = "image/jpeg",
                          });
}

TEST_CASE("Wayland portal response mapping preserves the public dialog contract",
          "[wayland][portal]") {
    const auto success = nk::detail::portal_result_from_response(0, std::string("/tmp/file.txt"));
    REQUIRE(success);
    REQUIRE(success.value() == "/tmp/file.txt");

    const auto cancelled = nk::detail::portal_result_from_response(1, std::nullopt);
    REQUIRE_FALSE(cancelled);
    REQUIRE(cancelled.error() == nk::FileDialogError::Cancelled);

    const auto missing_path = nk::detail::portal_result_from_response(0, std::nullopt);
    REQUIRE_FALSE(missing_path);
    REQUIRE(missing_path.error() == nk::FileDialogError::Failed);

    const auto failure = nk::detail::portal_result_from_response(2, std::nullopt);
    REQUIRE_FALSE(failure);
    REQUIRE(failure.error() == nk::FileDialogError::Failed);
}
