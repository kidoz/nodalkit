#pragma once

/// @file settings.h
/// @brief Persistent application settings: a key/value store with typed accessors.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nk {

/// Window position and size, for persisting and restoring geometry.
struct WindowGeometry {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/// A small persistent key/value settings store with typed accessors.
///
/// StateStore models transient UI state; Settings models the handful of durable
/// preferences an application keeps between runs — recent files, window
/// geometry, enum choices, paths. Values live in memory; call save() to write
/// them and load() to read them back.
///
/// The on-disk format is a UTF-8, line-based `key=value` file with
/// percent-escaping, deliberately dependency-free so NodalKit pulls in no JSON
/// or registry library. Applications that prefer another backend (registry,
/// TOML, a cloud sync store) can ignore this type and persist however they
/// like; nothing else in NodalKit depends on it.
///
/// Not thread-safe: own and use it on the UI thread, like StateStore. Marshal
/// worker-thread results back with EventLoop::post() before mutating.
class Settings {
public:
    /// Platform-appropriate settings file for @p app_name:
    ///   - Windows: `%APPDATA%\<app_name>\settings.conf`
    ///   - macOS:   `~/Library/Application Support/<app_name>/settings.conf`
    ///   - Linux:   `$XDG_CONFIG_HOME/<app_name>/settings.conf`
    ///              (falling back to `~/.config/<app_name>/settings.conf`)
    /// Falls back to `<app_name>/settings.conf` relative to the working
    /// directory if no home/appdata location can be resolved.
    [[nodiscard]] static std::filesystem::path default_path(std::string_view app_name);

    explicit Settings(std::filesystem::path file);

    [[nodiscard]] const std::filesystem::path& path() const { return file_; }

    /// Load values from disk, replacing the in-memory set. Returns false if the
    /// file does not exist (a fresh store) or cannot be read; true on success.
    bool load();

    /// Write the in-memory values to disk, creating parent directories as
    /// needed. Returns false on I/O failure.
    [[nodiscard]] bool save() const;

    // Typed accessors. The get_*() overloads return @p fallback when the key is
    // absent or its stored text cannot be parsed as the requested type.

    void set_string(std::string_view key, std::string_view value);
    [[nodiscard]] std::string get_string(std::string_view key,
                                         std::string_view fallback = {}) const;

    void set_int(std::string_view key, std::int64_t value);
    [[nodiscard]] std::int64_t get_int(std::string_view key, std::int64_t fallback = 0) const;

    void set_bool(std::string_view key, bool value);
    [[nodiscard]] bool get_bool(std::string_view key, bool fallback = false) const;

    void set_double(std::string_view key, double value);
    [[nodiscard]] double get_double(std::string_view key, double fallback = 0.0) const;

    void set_string_list(std::string_view key, const std::vector<std::string>& values);
    [[nodiscard]] std::vector<std::string> get_string_list(std::string_view key) const;

    /// Store a window's geometry under a single key.
    void set_window_geometry(std::string_view key, const WindowGeometry& geometry);
    [[nodiscard]] std::optional<WindowGeometry> get_window_geometry(std::string_view key) const;

    /// Recent-file list helper: inserts @p path at the front (most recent),
    /// removes any earlier duplicate of it, and trims to @p max_entries.
    void push_recent_file(std::string_view path,
                          std::size_t max_entries = 10,
                          std::string_view key = "recent_files");
    [[nodiscard]] std::vector<std::string>
    recent_files(std::string_view key = "recent_files") const;

    [[nodiscard]] bool contains(std::string_view key) const;
    void remove(std::string_view key);
    void clear();

    /// Number of stored keys.
    [[nodiscard]] std::size_t size() const { return values_.size(); }

private:
    std::filesystem::path file_;
    std::map<std::string, std::string, std::less<>> values_;
};

} // namespace nk
