/// @file settings_test.cpp
/// @brief Tests for the persistent Settings store.

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <nk/model/settings.h>
#include <string>
#include <vector>

namespace {

// A unique-per-test temp file that removes itself on scope exit.
struct TempSettingsFile {
    std::filesystem::path path;

    explicit TempSettingsFile(const std::string& name)
        : path(std::filesystem::temp_directory_path() / ("nk_settings_test_" + name) /
               "settings.conf") {
        std::error_code ec;
        std::filesystem::remove_all(path.parent_path(), ec);
    }

    ~TempSettingsFile() {
        std::error_code ec;
        std::filesystem::remove_all(path.parent_path(), ec);
    }
};

} // namespace

TEST_CASE("typed values round-trip in memory", "[settings]") {
    nk::Settings settings(std::filesystem::path("unused"));

    settings.set_string("name", "Cxbx");
    settings.set_int("width", 1280);
    settings.set_bool("fullscreen", true);
    settings.set_double("scale", 1.5);

    CHECK(settings.get_string("name") == "Cxbx");
    CHECK(settings.get_int("width") == 1280);
    CHECK(settings.get_bool("fullscreen") == true);
    CHECK(settings.get_double("scale") == 1.5);
}

TEST_CASE("doubles round-trip at full precision through save and load", "[settings]") {
    TempSettingsFile temp("double_precision");

    // A value that std::to_string (6 fractional digits) would truncate.
    constexpr double precise = 0.123456789123;
    constexpr double large = 1234567.890123456;

    {
        nk::Settings settings(temp.path);
        settings.set_double("precise", precise);
        settings.set_double("large", large);
        // In-memory must already be exact, not just after reload.
        CHECK(settings.get_double("precise") == precise);
        REQUIRE(settings.save());
    }

    nk::Settings reloaded(temp.path);
    REQUIRE(reloaded.load());
    CHECK(reloaded.get_double("precise") == precise);
    CHECK(reloaded.get_double("large") == large);
}

TEST_CASE("missing keys and bad parses return the fallback", "[settings]") {
    nk::Settings settings(std::filesystem::path("unused"));
    settings.set_string("not_a_number", "hello");

    CHECK(settings.get_string("absent", "default") == "default");
    CHECK(settings.get_int("absent", 42) == 42);
    CHECK(settings.get_bool("absent", true) == true);
    CHECK(settings.get_double("absent", 3.5) == 3.5);
    CHECK(settings.get_int("not_a_number", 7) == 7);
    CHECK(settings.get_double("not_a_number", 2.0) == 2.0);
}

TEST_CASE("values persist across save and load", "[settings]") {
    TempSettingsFile temp("persist");

    {
        nk::Settings settings(temp.path);
        settings.set_string("title", "Halo");
        settings.set_int("recent_count", 3);
        settings.set_bool("auto_convert", true);
        REQUIRE(settings.save());
    }

    nk::Settings reloaded(temp.path);
    REQUIRE(reloaded.load());
    CHECK(reloaded.get_string("title") == "Halo");
    CHECK(reloaded.get_int("recent_count") == 3);
    CHECK(reloaded.get_bool("auto_convert") == true);
}

TEST_CASE("loading a fresh (missing) file reports false and leaves an empty store", "[settings]") {
    TempSettingsFile temp("fresh");
    nk::Settings settings(temp.path);
    CHECK_FALSE(settings.load());
    CHECK(settings.size() == 0);
}

TEST_CASE("string lists survive separators and reserved characters", "[settings]") {
    TempSettingsFile temp("lists");

    const std::vector<std::string> tricky = {
        R"(C:\games\Halo, Combat Evolved.xbe)", // contains a comma
        "path=with=equals",                     // contains the record separator
        "percent%20literal",                    // contains a percent
        "line\nbreak",                          // contains a newline
        "",                                     // empty element
    };

    {
        nk::Settings settings(temp.path);
        settings.set_string_list("paths", tricky);
        REQUIRE(settings.save());
    }

    nk::Settings reloaded(temp.path);
    REQUIRE(reloaded.load());
    CHECK(reloaded.get_string_list("paths") == tricky);
}

TEST_CASE("empty and absent lists both yield an empty vector", "[settings]") {
    nk::Settings settings(std::filesystem::path("unused"));
    CHECK(settings.get_string_list("absent").empty());
    settings.set_string_list("empty", {});
    CHECK(settings.get_string_list("empty").empty());
}

TEST_CASE("recent files dedupe, promote, and trim", "[settings]") {
    nk::Settings settings(std::filesystem::path("unused"));

    settings.push_recent_file("a.xbe", 3);
    settings.push_recent_file("b.xbe", 3);
    settings.push_recent_file("c.xbe", 3);
    CHECK(settings.recent_files() == std::vector<std::string>{"c.xbe", "b.xbe", "a.xbe"});

    // Re-opening an older entry promotes it to the front without duplicating.
    settings.push_recent_file("a.xbe", 3);
    CHECK(settings.recent_files() == std::vector<std::string>{"a.xbe", "c.xbe", "b.xbe"});

    // Adding beyond the cap drops the oldest.
    settings.push_recent_file("d.xbe", 3);
    CHECK(settings.recent_files() == std::vector<std::string>{"d.xbe", "a.xbe", "c.xbe"});
}

TEST_CASE("window geometry round-trips and rejects malformed data", "[settings]") {
    TempSettingsFile temp("geometry");

    {
        nk::Settings settings(temp.path);
        settings.set_window_geometry(
            "main", nk::WindowGeometry{.x = 10, .y = 20, .width = 800, .height = 600});
        REQUIRE(settings.save());
    }

    nk::Settings reloaded(temp.path);
    REQUIRE(reloaded.load());
    const auto geometry = reloaded.get_window_geometry("main");
    REQUIRE(geometry.has_value());
    CHECK(geometry->x == 10);
    CHECK(geometry->y == 20);
    CHECK(geometry->width == 800);
    CHECK(geometry->height == 600);

    CHECK_FALSE(reloaded.get_window_geometry("absent").has_value());
    reloaded.set_string("broken", "not,four,ints");
    CHECK_FALSE(reloaded.get_window_geometry("broken").has_value());
}

TEST_CASE("contains, remove, and clear behave", "[settings]") {
    nk::Settings settings(std::filesystem::path("unused"));
    settings.set_string("a", "1");
    settings.set_string("b", "2");

    CHECK(settings.contains("a"));
    settings.remove("a");
    CHECK_FALSE(settings.contains("a"));
    CHECK(settings.contains("b"));

    settings.clear();
    CHECK(settings.size() == 0);
    CHECK_FALSE(settings.contains("b"));
}

TEST_CASE("load rejects a settings file larger than 1 MiB", "[settings]") {
    TempSettingsFile temp("oversized");
    std::filesystem::create_directories(temp.path.parent_path());
    {
        std::ofstream out(temp.path, std::ios::binary | std::ios::trunc);
        REQUIRE(out);
        out << "token=";
        out << std::string(1024U * 1024U, 'x');
        out << '\n';
    }
    nk::Settings settings(temp.path);
    settings.set_string("keep", "memory");
    CHECK_FALSE(settings.load());
    CHECK(settings.get_string("keep").empty());
}

TEST_CASE("default_path is under a per-app directory", "[settings]") {
    const std::filesystem::path path = nk::Settings::default_path("NodalKitTestApp");
    CHECK(path.filename() == "settings.conf");
    CHECK(path.parent_path().filename() == "NodalKitTestApp");
    CHECK(nk::Settings::default_path("").empty());
    CHECK(nk::Settings::default_path(".").empty());
    CHECK(nk::Settings::default_path("..").empty());
    CHECK(nk::Settings::default_path("a/b").empty());
    CHECK(nk::Settings::default_path("../escape").empty());
}

TEST_CASE("saved settings are owner-only on POSIX", "[settings]") {
    TempSettingsFile temp("permissions");
    nk::Settings settings(temp.path);
    settings.set_string("token", "value");
    REQUIRE(settings.save());
#if !defined(_WIN32)
    const auto permissions = std::filesystem::status(temp.path).permissions();
    const auto others = std::filesystem::perms::others_all | std::filesystem::perms::group_all;
    CHECK((permissions & others) == std::filesystem::perms::none);
    CHECK((permissions & std::filesystem::perms::owner_read) != std::filesystem::perms::none);
    CHECK((permissions & std::filesystem::perms::owner_write) != std::filesystem::perms::none);
#endif
}

TEST_CASE("Repeated saves replace content without leaving temporary files", "[settings]") {
    TempSettingsFile temp("atomic_replace");
    nk::Settings settings(temp.path);
    settings.set_string("value", "first");
    REQUIRE(settings.save());
    settings.set_string("value", "second");
    REQUIRE(settings.save());

    nk::Settings reloaded(temp.path);
    REQUIRE(reloaded.load());
    CHECK(reloaded.get_string("value") == "second");
    auto temporary = temp.path;
    temporary += ".tmp";
    CHECK_FALSE(std::filesystem::exists(temporary));
}
