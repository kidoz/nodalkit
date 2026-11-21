/// @file win32_native_test.cpp
/// @brief Windows-only behavioral tests for the native menu, spell checker,
/// drag-and-drop, IME conversion, and preference observation backends.
///
/// These tests exercise the public PlatformBackend surface that the Windows
/// backend overrides, plus the CF_HDROP file-drop parsing path and the
/// testable nk::detail helpers (accelerator building, multi-click counting,
/// preedit conversion, drag data object). They run on any Windows build (no
/// display or interactive input required). Tagged `[windows]` so they can be
/// filtered/selectively skipped on other platforms.

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
#include "../src/platform/windows/win32_backend.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
// ole2.h pulls in windows.h (EXTERN_C and friends) and sorts before
// shellapi.h, so the include sorter keeps the order the shell header needs.
#include <ole2.h>
#include <shellapi.h>
#include <string>
#include <vector>

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

TEST_CASE("Win32 text transfers use the extent of the locked allocation", "[windows][native]") {
    auto memory = GlobalAlloc(GMEM_MOVEABLE, 16 * sizeof(wchar_t));
    REQUIRE(memory != nullptr);
    const auto capacity = GlobalSize(memory) / sizeof(wchar_t);
    auto* data = static_cast<wchar_t*>(GlobalLock(memory));
    REQUIRE(data != nullptr);
    std::fill_n(data, capacity, L'x');
    GlobalUnlock(memory);
    CHECK_FALSE(nk::detail::read_global_unicode_text(memory).has_value());

    data = static_cast<wchar_t*>(GlobalLock(memory));
    REQUIRE(data != nullptr);
    data[2] = L'\0';
    GlobalUnlock(memory);
    CHECK(nk::detail::read_global_unicode_text(memory) == "xx");
    CHECK(GlobalFree(memory) == nullptr);

    auto oversized =
        GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, 16U * 1024U * 1024U + sizeof(wchar_t));
    REQUIRE(oversized != nullptr);
    CHECK_FALSE(nk::detail::read_global_unicode_text(oversized).has_value());
    CHECK(GlobalFree(oversized) == nullptr);
}

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

TEST_CASE("Win32 virtual-key translation covers accelerator keys", "[windows][native]") {
    using nk::KeyCode;
    const auto vk = nk::detail::win32_virtual_key_for_key;
    CHECK(vk(KeyCode::A) == 'A');
    CHECK(vk(KeyCode::Z) == 'Z');
    CHECK(vk(KeyCode::Num1) == '1');
    // Num0 sits after Num9 in the KeyCode ordering and must not fall through.
    CHECK(vk(KeyCode::Num0) == '0');
    CHECK(vk(KeyCode::F1) == VK_F1);
    CHECK(vk(KeyCode::F12) == VK_F12);
    CHECK(vk(KeyCode::Return) == VK_RETURN);
    CHECK(vk(KeyCode::NumpadEnter) == VK_RETURN);
    CHECK(vk(KeyCode::Space) == VK_SPACE);
    CHECK(vk(KeyCode::Minus) == VK_OEM_MINUS);
    CHECK(vk(KeyCode::LeftBracket) == VK_OEM_4);
    CHECK(vk(KeyCode::Numpad0) == VK_NUMPAD0);
    CHECK(vk(KeyCode::Numpad9) == VK_NUMPAD9);
    CHECK(vk(KeyCode::Up) == VK_UP);
    // Keys without a virtual-code equivalent must map to 0 so accelerator
    // building can reject them.
    CHECK(vk(KeyCode::Unknown) == 0);
    CHECK(vk(KeyCode::LeftCtrl) == 0);
    CHECK(vk(KeyCode::LeftSuper) == 0);
}

TEST_CASE("Win32 accelerator building maps menu shortcuts", "[windows][native]") {
    using nk::KeyCode;
    using nk::NativeMenuModifier;
    const auto build = nk::detail::win32_accel_for_shortcut;

    ACCEL accel{};
    REQUIRE(build({KeyCode::S, NativeMenuModifier::Ctrl | NativeMenuModifier::Shift}, 7, &accel));
    CHECK(accel.fVirt == (FVIRTKEY | FCONTROL | FSHIFT));
    CHECK(accel.key == 'S');
    CHECK(accel.cmd == 7);

    REQUIRE(build({KeyCode::F5, NativeMenuModifier::Alt}, 8, &accel));
    CHECK(accel.fVirt == (FVIRTKEY | FALT));
    CHECK(accel.key == VK_F5);

    // Super has no Windows accelerator spelling; a bare key would swallow
    // ordinary typing; modifier keys and Unknown have no virtual code.
    CHECK_FALSE(
        build({KeyCode::S, NativeMenuModifier::Ctrl | NativeMenuModifier::Super}, 7, &accel));
    CHECK_FALSE(build({KeyCode::S, NativeMenuModifier::None}, 7, &accel));
    CHECK_FALSE(build({KeyCode::LeftCtrl, NativeMenuModifier::Ctrl}, 7, &accel));
    CHECK_FALSE(build({KeyCode::Unknown, NativeMenuModifier::Ctrl}, 7, &accel));
    CHECK_FALSE(build({KeyCode::S, NativeMenuModifier::Ctrl}, 7, nullptr));
}

TEST_CASE("Win32 multi-click counting follows the double-click window", "[windows][native]") {
    nk::detail::Win32ClickCounter counter;
    constexpr std::uint64_t kDoubleMs = 500;
    constexpr int kWidth = 8; // half-extent used by the counter: 4
    constexpr int kHeight = 6;
    constexpr std::uint64_t kStart = 1'000;

    CHECK(counter.press(1, 100, 100, kStart, kDoubleMs, kWidth, kHeight) == 1);
    CHECK(counter.press(1, 102, 101, kStart + 100, kDoubleMs, kWidth, kHeight) == 2);
    CHECK(counter.press(1, 102, 101, kStart + 200, kDoubleMs, kWidth, kHeight) == 3);
    // Too much time elapsed: fresh click.
    CHECK(counter.press(1, 102, 101, kStart + 200 + kDoubleMs + 1, kDoubleMs, kWidth, kHeight) ==
          1);
    // Inside time but outside the double-click rectangle: fresh click.
    CHECK(counter.press(1, 106, 100, kStart + 1'000, kDoubleMs, kWidth, kHeight) == 2);
    CHECK(counter.press(1, 100, 100, kStart + 1'100, kDoubleMs, kWidth, kHeight) == 1);
    CHECK(counter.press(1, 100, 100, kStart + 1'200, kDoubleMs, kWidth, kHeight) == 2);
    // A different button restarts the sequence.
    CHECK(counter.press(2, 100, 100, kStart + 1'300, kDoubleMs, kWidth, kHeight) == 1);
    CHECK(counter.press(1, 100, 100, kStart + 1'400, kDoubleMs, kWidth, kHeight) == 1);
}

TEST_CASE("Win32 preedit conversion matches the toolkit contract", "[windows][native]") {
    const auto convert = nk::detail::win32_preedit_event_from_composition;
    using Type = nk::TextInputEvent::Type;

    // Three BMP characters: cursor offsets are UTF-8 byte offsets.
    const auto bmp = convert(L"あいう", 1);
    CHECK(bmp.type == Type::Preedit);
    CHECK(bmp.text == "あいう");
    CHECK(bmp.selection_start == 3);
    CHECK(bmp.selection_end == 3);

    // Astral character: one code point spans two wchars and four UTF-8 bytes.
    const auto astral = convert(L"𠮷あ", 2);
    CHECK(astral.type == Type::Preedit);
    CHECK(astral.selection_start == 4);
    CHECK(astral.selection_end == 4);

    // Negative or out-of-range cursors clamp to the end; empty clears.
    const auto clamped = convert(L"あいう", -1);
    CHECK(clamped.selection_start == 9);
    CHECK(clamped.selection_end == 9);
    const auto overflow = convert(L"あいう", 99);
    CHECK(overflow.selection_start == 9);
    CHECK(overflow.selection_end == 9);
    const auto cleared = convert(L"", 0);
    CHECK(cleared.type == Type::ClearPreedit);
    CHECK(cleared.text.empty());
}

TEST_CASE("Win32 drag data object exposes files as CF_HDROP", "[windows][native]") {
    std::vector<std::filesystem::path> files{L"C:\\tmp\\alpha.txt", L"D:\\data\\beta.bin"};
    auto* data_object = nk::detail::win32_make_drag_data_object(nk::DragPayload::from_files(files));
    REQUIRE(data_object != nullptr);

    FORMATETC hdrop{
        .cfFormat = CF_HDROP,
        .ptd = nullptr,
        .dwAspect = DVASPECT_CONTENT,
        .lindex = -1,
        .tymed = TYMED_HGLOBAL,
    };
    CHECK(data_object->QueryGetData(&hdrop) == S_OK);

    STGMEDIUM medium{};
    REQUIRE(data_object->GetData(&hdrop, &medium) == S_OK);
    REQUIRE(medium.tymed == TYMED_HGLOBAL);
    const UINT count = DragQueryFileW(static_cast<HDROP>(medium.hGlobal), 0xFFFFFFFF, nullptr, 0);
    REQUIRE(count == 2);
    std::wstring first(DragQueryFileW(static_cast<HDROP>(medium.hGlobal), 0, nullptr, 0), L'\0');
    DragQueryFileW(
        static_cast<HDROP>(medium.hGlobal), 0, first.data(), static_cast<UINT>(first.size()) + 1);
    CHECK(first == L"C:\\tmp\\alpha.txt");
    ReleaseStgMedium(&medium);

    // A file payload offers no text format.
    FORMATETC text{
        .cfFormat = CF_UNICODETEXT,
        .ptd = nullptr,
        .dwAspect = DVASPECT_CONTENT,
        .lindex = -1,
        .tymed = TYMED_HGLOBAL,
    };
    CHECK(FAILED(data_object->QueryGetData(&text)));
    data_object->Release();
}

TEST_CASE("Win32 drag data object exposes text as CF_UNICODETEXT", "[windows][native]") {
    auto* data_object =
        nk::detail::win32_make_drag_data_object(nk::DragPayload::from_text("hello win32"));
    REQUIRE(data_object != nullptr);

    FORMATETC text{
        .cfFormat = CF_UNICODETEXT,
        .ptd = nullptr,
        .dwAspect = DVASPECT_CONTENT,
        .lindex = -1,
        .tymed = TYMED_HGLOBAL,
    };
    STGMEDIUM medium{};
    REQUIRE(data_object->GetData(&text, &medium) == S_OK);
    CHECK(nk::detail::read_global_unicode_text(medium.hGlobal) == "hello win32");
    ReleaseStgMedium(&medium);

    // The enumerator walks exactly the offered formats, GET direction only.
    IEnumFORMATETC* enumerator = nullptr;
    REQUIRE(data_object->EnumFormatEtc(DATADIR_GET, &enumerator) == S_OK);
    FORMATETC seen[4]{};
    ULONG fetched = 0;
    REQUIRE(enumerator->Next(4, seen, &fetched) == S_FALSE);
    CHECK(fetched == 1);
    CHECK(seen[0].cfFormat == CF_UNICODETEXT);
    CHECK(enumerator->Reset() == S_OK);
    enumerator->Release();
    data_object->Release();

    // In-process application data cannot cross the OLE boundary.
    auto* in_process = nk::detail::win32_make_drag_data_object(
        nk::DragPayload::from_application_data("application/x-test", 7));
    CHECK(in_process == nullptr);
}

TEST_CASE("Win32 backend observes system preferences through a hidden window",
          "[windows][native]") {
    auto backend = nk::PlatformBackend::create();
    REQUIRE(backend != nullptr);
    REQUIRE(backend->initialize());

    REQUIRE(backend->supports_system_preferences_observation());
    bool notified = false;
    backend->start_system_preferences_observation(
        [&notified](const nk::SystemPreferences&) { notified = true; });
    // Starting observation seeds the snapshot; it must not emit on its own.
    CHECK_FALSE(notified);
    // Start/stop cycles must be clean (window destroyed, no leak into a poll).
    backend->stop_system_preferences_observation();
    backend->start_system_preferences_observation([](const nk::SystemPreferences&) {});
    backend->stop_system_preferences_observation();
    CHECK_FALSE(notified);

    backend->shutdown();
}

#endif // defined(_WIN32)
