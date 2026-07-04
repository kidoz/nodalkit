/// @file log_view_test.cpp
/// @brief Tests for the append-only LogView data model (headless).

#include <catch2/catch_test_macros.hpp>
#include <nk/widgets/log_view.h>
#include <string>
#include <vector>

TEST_CASE("append grows the buffer and strips a single trailing newline", "[log_view]") {
    auto log = nk::LogView::create();
    log->append_line("first");
    log->append_line("second\n");
    log->append_line("third\n\n"); // only one trailing newline is stripped

    REQUIRE(log->line_count() == 3);
    CHECK(log->line_text(0) == "first");
    CHECK(log->line_text(1) == "second");
    CHECK(log->line_text(2) == "third\n");
}

TEST_CASE("severity is retained per line", "[log_view]") {
    auto log = nk::LogView::create();
    log->append_line("ok", nk::LogSeverity::Success);
    log->append_line("uh oh", nk::LogSeverity::Warning);
    log->append_line("boom", nk::LogSeverity::Error);

    CHECK(log->line_severity(0) == nk::LogSeverity::Success);
    CHECK(log->line_severity(1) == nk::LogSeverity::Warning);
    CHECK(log->line_severity(2) == nk::LogSeverity::Error);
    CHECK(log->line_severity(99) == nk::LogSeverity::Normal); // out of range
}

TEST_CASE("the retention cap drops the oldest lines", "[log_view]") {
    auto log = nk::LogView::create();
    log->set_max_lines(3);
    for (int i = 0; i < 6; ++i) {
        log->append_line("line " + std::to_string(i));
    }

    REQUIRE(log->line_count() == 3);
    CHECK(log->line_text(0) == "line 3");
    CHECK(log->line_text(1) == "line 4");
    CHECK(log->line_text(2) == "line 5");
}

TEST_CASE("shrinking max_lines trims immediately", "[log_view]") {
    auto log = nk::LogView::create();
    for (int i = 0; i < 5; ++i) {
        log->append_line("line " + std::to_string(i));
    }
    log->set_max_lines(2);
    REQUIRE(log->line_count() == 2);
    CHECK(log->line_text(0) == "line 3");
    CHECK(log->line_text(1) == "line 4");
}

TEST_CASE("search finds matching lines case-insensitively", "[log_view]") {
    auto log = nk::LogView::create();
    log->append_line("Booting kernel");
    log->append_line("HLE match: XapiInitProcess");
    log->append_line("Booting complete");
    log->append_line("idle");

    const std::vector<std::size_t> hits = log->search("boot");
    CHECK(hits == std::vector<std::size_t>{0, 2});

    // Empty query clears matches.
    CHECK(log->search("").empty());
    CHECK(log->matches().empty());

    // No hits.
    CHECK(log->search("nonexistent").empty());
}

TEST_CASE("next/previous cycle through matches", "[log_view]") {
    auto log = nk::LogView::create();
    for (int i = 0; i < 5; ++i) {
        log->append_line(i % 2 == 0 ? "hit" : "miss");
    }
    const std::vector<std::size_t> hits = log->search("hit");
    REQUIRE(hits == std::vector<std::size_t>{0, 2, 4});

    // Stepping and wrapping must not crash and stays within the match set.
    log->next_match();
    log->next_match();
    log->next_match(); // wraps back to the first
    log->previous_match();
    SUCCEED("match navigation stayed in bounds");
}

TEST_CASE("export joins lines with newlines", "[log_view]") {
    auto log = nk::LogView::create();
    log->append_line("a");
    log->append_line("b");
    log->append_line("c");
    CHECK(log->export_text() == "a\nb\nc");
}

TEST_CASE("clear empties the buffer and search state", "[log_view]") {
    auto log = nk::LogView::create();
    log->append_line("something");
    log->search("something");
    log->clear();

    CHECK(log->line_count() == 0);
    CHECK(log->matches().empty());
    CHECK(log->export_text().empty());
}

TEST_CASE("auto-scroll toggles and defaults on", "[log_view]") {
    auto log = nk::LogView::create();
    CHECK(log->auto_scroll());
    log->set_auto_scroll(false);
    CHECK_FALSE(log->auto_scroll());
}
