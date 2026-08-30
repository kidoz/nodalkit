/// @file win32_native_test.cpp
/// @brief Windows-only behavioral tests for the native menu, spell checker, and
/// drag-and-drop payload extraction backends.
///
/// These tests exercise the public PlatformBackend surface that the Windows
/// backend overrides, plus the CF_HDROP file-drop parsing path. They run on any
/// Windows build (no display or interactive drop required). Tagged
/// `[windows]` so they can be filtered/selectively skipped on other platforms.

#if defined(_WIN32)

#include <catch2/catch_test_macros.hpp>
#include <nk/platform/native_menu.h>
#include <nk/platform/platform_backend.h>
#include <nk/platform/spell_checker.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
// windows.h must precede shellapi.h: the shell header uses EXTERN_C, which
// only windows.h defines.
#include <shellapi.h>
#include <string>
#include <vector>
#include <windows.h>

namespace {

/// Local mirror of the Win32 DROPFILES header layout (offset of the path list +
/// the fWide flag), since the SDK's shellapi.h does not expose DROPFILES
/// uniformly across target versions. This matches the documented CF_HDROP
/// layout that DragQueryFileW consumes.
#pragma pack(push, 1)

struct LocalDropFiles {
    UINT pFiles;
    POINT pt;
    BOOL fNC;
    BOOL fWide;
};

#pragma pack(pop)

/// RAII owner of a GlobalAlloc'd CF_HDROP blob, kept so the synthesized payload
/// stays valid for the duration of a DragQueryFileW enumeration.
class OwnedGlobalDrop {
public:
    explicit OwnedGlobalDrop(std::vector<std::wstring> paths) {
        // CF_HDROP layout: DROPFILES header (offset to path list in pFiles, fWide)
        // followed by NUL-terminated wide strings and a terminating extra NUL.
        std::size_t chars = 1; // final terminating NUL
        for (const auto& p : paths) {
            chars += p.size() + 1;
        }
        const std::size_t bytes = sizeof(LocalDropFiles) + chars * sizeof(wchar_t);
        handle_ = GlobalAlloc(GMEM_MOVEABLE, bytes);
        REQUIRE(handle_ != nullptr);
        auto* drop_files = static_cast<LocalDropFiles*>(GlobalLock(handle_));
        REQUIRE(drop_files != nullptr);
        drop_files->pFiles = sizeof(LocalDropFiles);
        drop_files->pt.x = 0;
        drop_files->pt.y = 0;
        drop_files->fNC = FALSE;
        drop_files->fWide = TRUE;
        auto* out = reinterpret_cast<wchar_t*>(reinterpret_cast<std::byte*>(drop_files) +
                                               sizeof(LocalDropFiles));
        for (auto& p : paths) {
            std::wmemcpy(out, p.c_str(), p.size());
            out += p.size();
            *out++ = L'\0';
        }
        *out = L'\0';
        GlobalUnlock(handle_);
    }

    ~OwnedGlobalDrop() {
        if (handle_ != nullptr) {
            GlobalFree(handle_);
        }
    }

    OwnedGlobalDrop(const OwnedGlobalDrop&) = delete;
    OwnedGlobalDrop& operator=(const OwnedGlobalDrop&) = delete;

    [[nodiscard]] HDROP handle() const { return static_cast<HDROP>(handle_); }

private:
    HGLOBAL handle_ = nullptr;
};

} // namespace

TEST_CASE("Win32 backend reports native app-menu support", "[windows][native]") {
    auto backend = nk::PlatformBackend::create();
    REQUIRE(backend != nullptr);
    REQUIRE(backend->supports_native_app_menu());
}

TEST_CASE("Win32 backend accepts a native app-menu model", "[windows][native]") {
    auto backend = nk::PlatformBackend::create();
    REQUIRE(backend != nullptr);

    std::vector<nk::NativeMenu> menus;
    nk::NativeMenu file_menu{.title = "File", .items = {}};
    file_menu.items.push_back(nk::NativeMenuItem::action(
        "Open", "file.open", nk::NativeMenuShortcut{nk::KeyCode::O, nk::NativeMenuModifier::Ctrl}));
    file_menu.items.push_back(nk::NativeMenuItem::make_separator());
    file_menu.items.push_back(nk::NativeMenuItem::action("Quit", "app.quit"));
    menus.push_back(std::move(file_menu));

    bool handler_invoked = false;
    backend->set_native_app_menu(menus, [&](std::string_view action) {
        handler_invoked = true;
        REQUIRE(action == "file.open");
    });
    // Re-asserting the handler round-trips is covered by WM_COMMAND dispatch in
    // the interactive showcase; here we only validate the call is well-formed.
    REQUIRE_FALSE(handler_invoked);
}

TEST_CASE("Win32 backend exposes a spell checker instance", "[windows][native]") {
    auto backend = nk::PlatformBackend::create();
    REQUIRE(backend != nullptr);
    auto* checker = backend->spell_checker();
    REQUIRE(checker != nullptr);

    SECTION("empty input yields no ranges without crashing") {
        const auto ranges = checker->check("");
        REQUIRE(ranges.empty());
    }
    SECTION("suggestions for empty word is empty and does not crash") {
        const auto suggestions = checker->suggestions("");
        REQUIRE(suggestions.empty());
    }
}

TEST_CASE("DragQueryFileW enumerates a synthesized CF_HDROP payload", "[windows][native]") {
    OwnedGlobalDrop drop({L"C:\\tmp\\alpha.txt", L"D:\\data\\beta.bin"});
    const HDROP handle = drop.handle();

    const UINT count = DragQueryFileW(handle, 0xFFFFFFFF, nullptr, 0);
    REQUIRE(count == 2);

    std::vector<std::wstring> paths;
    for (UINT i = 0; i < count; ++i) {
        const UINT length = DragQueryFileW(handle, i, nullptr, 0);
        REQUIRE(length > 0);
        std::wstring path(length, L'\0');
        DragQueryFileW(handle, i, path.data(), length + 1);
        paths.push_back(std::move(path));
    }
    REQUIRE(paths.size() == 2);
    REQUIRE(paths[0] == L"C:\\tmp\\alpha.txt");
    REQUIRE(paths[1] == L"D:\\data\\beta.bin");
}

#endif // defined(_WIN32)
