/// @file message_catalog_test.cpp
/// @brief Tests for translated-message loading and placeholder expansion.

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <ios>
#include <nk/l10n/message_catalog.h>
#include <string>
#include <string_view>
#include <system_error>

namespace {

std::filesystem::path fixture_path(std::string_view filename) {
    auto source_relative =
        std::filesystem::path(__FILE__).parent_path() / "fixtures" / "l10n" / filename;
    if (std::filesystem::exists(source_relative)) {
        return source_relative;
    }
    return std::filesystem::current_path() / "tests" / "fixtures" / "l10n" / filename;
}

class TempCatalogFile {
public:
    explicit TempCatalogFile(std::string_view name)
        : path_(std::filesystem::temp_directory_path() /
                ("nk_message_catalog_" + std::string(name)) / "messages.nkstrings") {
        std::error_code error;
        std::filesystem::remove_all(path_.parent_path(), error);
        std::filesystem::create_directories(path_.parent_path());
    }

    ~TempCatalogFile() {
        std::error_code error;
        std::filesystem::remove_all(path_.parent_path(), error);
    }

    TempCatalogFile(const TempCatalogFile&) = delete;
    TempCatalogFile& operator=(const TempCatalogFile&) = delete;
    TempCatalogFile(TempCatalogFile&&) = delete;
    TempCatalogFile& operator=(TempCatalogFile&&) = delete;

    void write(std::string_view contents) const {
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

} // namespace

TEST_CASE("catalog loads English messages and named arguments", "[l10n]") {
    nk::MessageCatalog catalog;
    REQUIRE(catalog.load(fixture_path("en.nkstrings")));

    CHECK(catalog.size() == 5);
    CHECK(catalog.contains("app.title"));
    CHECK_FALSE(catalog.contains("missing"));
    CHECK(catalog.translate("app.title") == "Localization example");
    CHECK(catalog.translate("welcome.user", {{"user", "Ada"}}) == "Welcome, Ada!");
    CHECK(catalog.translate("files.selected", {{"count", "3"}, {"folder", "Documents"}}) ==
          "3 files selected from Documents");
    CHECK(catalog.translate("missing.message") == "missing.message");
}

TEST_CASE("catalog loads UTF-8 translations with reordered arguments", "[l10n]") {
    nk::MessageCatalog catalog;
    REQUIRE(catalog.load(fixture_path("ru.nkstrings")));

    CHECK(catalog.translate("app.title") == "Пример локализации");
    CHECK(catalog.translate("welcome.user", {{"user", "Ада"}}) == "Добро пожаловать, Ада!");
    CHECK(catalog.translate("files.selected", {{"count", "3"}, {"folder", "Документы"}}) ==
          "Из папки Документы выбрано файлов: 3");
}

TEST_CASE("catalog expands escapes and literal braces without recursive expansion", "[l10n]") {
    nk::MessageCatalog catalog;
    REQUIRE(catalog.load(fixture_path("en.nkstrings")));

    CHECK(catalog.translate("escaped") == "Line one\nLine two\t#1 = ready");
    CHECK(catalog.translate("literal.braces", {{"name", "{user}"}, {"extra", "ignored"}}) ==
          "Use {name} for {user}; repeat {user}.");
    CHECK(catalog.translate("files.selected", {{"count", "2"}}) ==
          "2 files selected from {folder}");
}

TEST_CASE("catalog treats malformed placeholders as literal text", "[l10n]") {
    const TempCatalogFile file("malformed_patterns");
    file.write("unmatched=Before {open\n"
               "invalid=Empty {} and {bad name}\n"
               "literal=Braces {{x}} and stray }\n");

    nk::MessageCatalog catalog;
    REQUIRE(catalog.load(file.path()));
    CHECK(catalog.translate("unmatched") == "Before {open");
    CHECK(catalog.translate("invalid") == "Empty {} and {bad name}");
    CHECK(catalog.translate("literal") == "Braces {x} and stray }");
}

TEST_CASE("catalog accepts BOM CRLF comments and escaped separators", "[l10n]") {
    const TempCatalogFile file("syntax");
    file.write("\xEF\xBB\xBF  # comment\r\n"
               "\r\n"
               "greeting\\=formal = Hello\\# \\= world\r\n");

    nk::MessageCatalog catalog;
    REQUIRE(catalog.load(file.path()));
    CHECK(catalog.translate("greeting=formal") == "Hello# = world");
}

TEST_CASE("failed reload reports errors and preserves the previous catalog", "[l10n]") {
    const TempCatalogFile file("parse_errors");
    file.write("stable=Still here\n");
    nk::MessageCatalog catalog;
    REQUIRE(catalog.load(file.path()));

    const auto expect_error = [&](std::string_view contents) {
        file.write(contents);
        auto result = catalog.load(file.path());
        CHECK_FALSE(result);
        CHECK(catalog.size() == 1);
        CHECK(catalog.translate("stable") == "Still here");
    };

    expect_error("missing separator\n");
    expect_error("=empty ID\n");
    expect_error("duplicate=one\nduplicate=two\n");
    expect_error("bad=unknown\\q\n");
    expect_error(R"(bad=trailing\)");
    expect_error("bad\\q=invalid key escape\n");
}

TEST_CASE("catalog rejects unreadable and invalid UTF-8 files atomically", "[l10n]") {
    const TempCatalogFile file("invalid_utf8");
    file.write("stable=Still here\n");
    nk::MessageCatalog catalog;
    REQUIRE(catalog.load(file.path()));

    CHECK_FALSE(catalog.load(file.path().parent_path() / "missing.nkstrings"));
    std::string invalid{"bad="};
    invalid.push_back(static_cast<char>(0xC0));
    invalid.push_back(static_cast<char>(0xAF));
    file.write(invalid);
    CHECK_FALSE(catalog.load(file.path()));
    CHECK(catalog.translate("stable") == "Still here");
}

TEST_CASE("clear removes all messages", "[l10n]") {
    nk::MessageCatalog catalog;
    REQUIRE(catalog.load(fixture_path("en.nkstrings")));

    catalog.clear();
    CHECK(catalog.size() == 0);
    CHECK_FALSE(catalog.contains("app.title"));
    CHECK(catalog.translate("app.title") == "app.title");
}
