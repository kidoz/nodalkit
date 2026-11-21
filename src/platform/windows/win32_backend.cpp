#include "win32_backend.h"

#include "uia_provider.h"
#include "win32_spell_checker.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <nk/foundation/logging.h>
#include <nk/platform/application.h>
#include <nk/platform/drag_drop.h>
#include <nk/platform/native_menu.h>
#include <nk/platform/spell_checker.h>
#include <nk/platform/window.h>
#include <nk/runtime/event_loop.h>
#include <ranges>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// windows.h must precede the Windows-specific headers below.
#include <dwmapi.h>
#include <imm.h>
#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shobjidl.h>
#include <windows.h>
#include <windowsx.h>
#include <winternl.h>
#include <wrl/client.h>

namespace nk {

namespace {

using Microsoft::WRL::ComPtr;

constexpr UINT kWakeMessage = WM_APP + 1;
constexpr const wchar_t* kWindowClassName = L"NodalKitWindow";
constexpr const wchar_t* kPreferencesWindowClassName = L"NodalKitPreferencesWindow";
// WM_THEMECHANGE (0x031A) is not exposed uniformly across SDK versions.
constexpr UINT kWmThemeChange = 0x031A;
// DWMWA_USE_IMMERSIVE_DARK_MODE shipped as attribute 19 on early Windows 10
// builds and was renumbered to 20 starting with build 19041. Both are tried so
// the native title bar and menu bar follow the system color scheme on either
// generation.
constexpr DWORD kDwmwaImmersiveDarkMode = 20;
constexpr DWORD kDwmwaImmersiveDarkModeLegacy = 19;

/// Local mirror of the Win32 DROPFILES header layout, since the SDK's
/// shellapi.h does not expose DROPFILES uniformly across target versions. This
/// matches the documented CF_HDROP layout DragQueryFileW consumes.
#pragma pack(push, 1)

struct LocalDropFiles {
    UINT pFiles;
    POINT pt;
    BOOL fNC;
    BOOL fWide;
};

#pragma pack(pop)

std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8,
                            MB_ERR_INVALID_CHARS,
                            text.data(),
                            static_cast<int>(text.size()),
                            wide.data(),
                            required) != required) {
        return {};
    }
    return wide;
}

std::string wide_to_utf8(std::wstring_view text) {
    if (text.empty()) {
        return {};
    }
    const int required = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string utf8(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8,
                            0,
                            text.data(),
                            static_cast<int>(text.size()),
                            utf8.data(),
                            required,
                            nullptr,
                            nullptr) != required) {
        return {};
    }
    return utf8;
}

float query_dpi_scale(HWND hwnd) {
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static const auto get_dpi_for_window = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (get_dpi_for_window != nullptr && hwnd != nullptr) {
        return static_cast<float>(get_dpi_for_window(hwnd)) / 96.0F;
    }

    HDC dc = hwnd != nullptr ? GetDC(hwnd) : GetDC(nullptr);
    const int dpi = dc != nullptr ? GetDeviceCaps(dc, LOGPIXELSX) : 96;
    if (dc != nullptr) {
        hwnd != nullptr ? ReleaseDC(hwnd, dc) : ReleaseDC(nullptr, dc);
    }
    return static_cast<float>(dpi) / 96.0F;
}

float current_system_scale() {
    return query_dpi_scale(nullptr);
}

float scale_factor_from_dpi(UINT dpi) {
    return static_cast<float>(std::max<UINT>(dpi, 96U)) / 96.0F;
}

bool nearly_equal(float lhs, float rhs) {
    return std::fabs(lhs - rhs) <= 0.001F;
}

Modifiers query_modifiers() {
    Modifiers modifiers = Modifiers::None;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        modifiers = modifiers | Modifiers::Shift;
    }
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0) {
        modifiers = modifiers | Modifiers::Ctrl;
    }
    if ((GetKeyState(VK_MENU) & 0x8000) != 0) {
        modifiers = modifiers | Modifiers::Alt;
    }
    if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0) {
        modifiers = modifiers | Modifiers::Super;
    }
    return modifiers;
}

KeyCode translate_key_code(WPARAM virtual_key, LPARAM lparam) {
    const bool extended = (lparam & 0x01000000) != 0;
    if (virtual_key >= 'A' && virtual_key <= 'Z') {
        return static_cast<KeyCode>(static_cast<int>(KeyCode::A) +
                                    static_cast<int>(virtual_key - 'A'));
    }
    if (virtual_key >= '0' && virtual_key <= '9') {
        return static_cast<KeyCode>(static_cast<int>(KeyCode::Num0) +
                                    static_cast<int>(virtual_key - '0'));
    }
    if (virtual_key >= VK_F1 && virtual_key <= VK_F12) {
        return static_cast<KeyCode>(static_cast<int>(KeyCode::F1) +
                                    static_cast<int>(virtual_key - VK_F1));
    }
    if (virtual_key >= VK_NUMPAD0 && virtual_key <= VK_NUMPAD9) {
        return static_cast<KeyCode>(static_cast<int>(KeyCode::Numpad0) +
                                    static_cast<int>(virtual_key - VK_NUMPAD0));
    }

    switch (virtual_key) {
    case VK_RETURN:
        return extended ? KeyCode::NumpadEnter : KeyCode::Return;
    case VK_ESCAPE:
        return KeyCode::Escape;
    case VK_BACK:
        return KeyCode::Backspace;
    case VK_TAB:
        return KeyCode::Tab;
    case VK_SPACE:
        return KeyCode::Space;
    case VK_OEM_MINUS:
        return KeyCode::Minus;
    case VK_OEM_PLUS:
        return KeyCode::Equals;
    case VK_OEM_4:
        return KeyCode::LeftBracket;
    case VK_OEM_6:
        return KeyCode::RightBracket;
    case VK_OEM_5:
        return KeyCode::Backslash;
    case VK_OEM_1:
        return KeyCode::Semicolon;
    case VK_OEM_7:
        return KeyCode::Apostrophe;
    case VK_OEM_3:
        return KeyCode::Grave;
    case VK_OEM_COMMA:
        return KeyCode::Comma;
    case VK_OEM_PERIOD:
        return KeyCode::Period;
    case VK_OEM_2:
        return KeyCode::Slash;
    case VK_CAPITAL:
        return KeyCode::CapsLock;
    case VK_PRINT:
    case VK_SNAPSHOT:
        return KeyCode::PrintScreen;
    case VK_SCROLL:
        return KeyCode::ScrollLock;
    case VK_PAUSE:
        return KeyCode::Pause;
    case VK_INSERT:
        return KeyCode::Insert;
    case VK_HOME:
        return KeyCode::Home;
    case VK_PRIOR:
        return KeyCode::PageUp;
    case VK_DELETE:
        return KeyCode::Delete;
    case VK_END:
        return KeyCode::End;
    case VK_NEXT:
        return KeyCode::PageDown;
    case VK_RIGHT:
        return KeyCode::Right;
    case VK_LEFT:
        return KeyCode::Left;
    case VK_DOWN:
        return KeyCode::Down;
    case VK_UP:
        return KeyCode::Up;
    case VK_NUMLOCK:
        return KeyCode::NumLock;
    case VK_DIVIDE:
        return KeyCode::NumpadDivide;
    case VK_MULTIPLY:
        return KeyCode::NumpadMultiply;
    case VK_SUBTRACT:
        return KeyCode::NumpadMinus;
    case VK_ADD:
        return KeyCode::NumpadPlus;
    case VK_DECIMAL:
        return KeyCode::NumpadPeriod;
    case VK_LCONTROL:
        return KeyCode::LeftCtrl;
    case VK_RCONTROL:
        return KeyCode::RightCtrl;
    case VK_LSHIFT:
        return KeyCode::LeftShift;
    case VK_RSHIFT:
        return KeyCode::RightShift;
    case VK_LMENU:
        return KeyCode::LeftAlt;
    case VK_RMENU:
        return KeyCode::RightAlt;
    case VK_LWIN:
        return KeyCode::LeftSuper;
    case VK_RWIN:
        return KeyCode::RightSuper;
    default:
        return KeyCode::Unknown;
    }
}

Color query_accent_color() {
    DWORD accent = 0;
    BOOL opaque = FALSE;
    if (FAILED(DwmGetColorizationColor(&accent, &opaque))) {
        return {};
    }
    return {
        ((accent >> 16) & 0xFF) / 255.0F,
        ((accent >> 8) & 0xFF) / 255.0F,
        (accent & 0xFF) / 255.0F,
        opaque ? 1.0F : ((accent >> 24) & 0xFF) / 255.0F,
    };
}

ColorScheme query_color_scheme() {
    DWORD apps_use_light_theme = 1;
    DWORD value_size = sizeof(apps_use_light_theme);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &apps_use_light_theme,
                     &value_size) == ERROR_SUCCESS &&
        apps_use_light_theme == 0) {
        return ColorScheme::Dark;
    }
    return ColorScheme::Light;
}

/// Match the native window frame (title bar, menu bar) to the system color
/// scheme. Call after window creation and again whenever the scheme changes.
void apply_color_scheme_chrome(HWND hwnd, ColorScheme scheme) {
    if (hwnd == nullptr) {
        return;
    }
    const BOOL dark = scheme == ColorScheme::Dark ? TRUE : FALSE;
    if (FAILED(DwmSetWindowAttribute(hwnd, kDwmwaImmersiveDarkMode, &dark, sizeof(dark)))) {
        (void)DwmSetWindowAttribute(hwnd, kDwmwaImmersiveDarkModeLegacy, &dark, sizeof(dark));
    }
}

SystemPreferences query_system_preferences() {
    SystemPreferences preferences;
    preferences.platform_family = PlatformFamily::Windows;
    preferences.desktop_environment = DesktopEnvironment::Other;
    preferences.color_scheme = query_color_scheme();

    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0) &&
        (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0) {
        preferences.contrast = ContrastPreference::High;
    }

    BOOL animations_enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations_enabled, 0) &&
        animations_enabled == FALSE) {
        preferences.motion = MotionPreference::Reduced;
    }

    // Read the real OS version through RtlGetVersion. GetVersionEx is shimmed by
    // application compatibility and would misreport the build on Windows 10/11.
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll"); ntdll != nullptr) {
        auto rtl_get_version =
            reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
        if (rtl_get_version != nullptr) {
            RTL_OSVERSIONINFOW version_info{};
            version_info.dwOSVersionInfoSize = sizeof(version_info);
            if (rtl_get_version(&version_info) == 0) {
                preferences.os_version_major = static_cast<int>(version_info.dwMajorVersion);
                preferences.os_version_build = static_cast<int>(version_info.dwBuildNumber);
            }
        }
    }

    // Mica is a Windows 11 (build 22000+) system material; older builds fall back
    // to an opaque surface fill. DWM composition is always available on the
    // supported Windows versions, so no host reports the None capability here.
    preferences.backdrop = preferences.os_version_build >= 22000 ? BackdropCapability::Material
                                                                 : BackdropCapability::Opaque;

    DWORD enable_transparency = 1;
    DWORD transparency_size = sizeof(enable_transparency);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"EnableTransparency",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &enable_transparency,
                     &transparency_size) == ERROR_SUCCESS &&
        enable_transparency == 0) {
        preferences.transparency = TransparencyPreference::Reduced;
    }

    // Accessibility text scaling (Settings > Accessibility > Text size) is stored
    // as a percentage from 100 to 225.
    DWORD text_scale = 100;
    DWORD text_scale_size = sizeof(text_scale);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Accessibility",
                     L"TextScaleFactor",
                     RRF_RT_REG_DWORD,
                     nullptr,
                     &text_scale,
                     &text_scale_size) == ERROR_SUCCESS &&
        text_scale >= 100) {
        preferences.text_scale_factor = static_cast<float>(text_scale) / 100.0F;
    }

    preferences.accent_color = query_accent_color();
    return preferences;
}

std::wstring default_extension_for_filters(const std::vector<std::string>& filters) {
    for (const auto& item : filters) {
        std::string_view ext = item;
        if (ext.empty() || ext.find('/') != std::string_view::npos) {
            continue;
        }
        if (ext.starts_with("*.")) {
            ext = ext.substr(2);
        } else if (ext.starts_with(".")) {
            ext = ext.substr(1);
        }
        if (ext.empty() || ext.find('*') != std::string_view::npos ||
            ext.find('?') != std::string_view::npos) {
            continue;
        }
        return utf8_to_wide(ext);
    }
    return {};
}

struct FileDialogFilterStorage {
    std::vector<std::wstring> labels;
    std::vector<std::wstring> patterns;
    std::vector<COMDLG_FILTERSPEC> specs;
};

FileDialogFilterStorage build_file_dialog_filters(const std::vector<std::string>& filters) {
    FileDialogFilterStorage storage;
    storage.labels.reserve(filters.size());
    storage.patterns.reserve(filters.size());

    for (const auto& item : filters) {
        auto pattern = utf8_to_wide(item);
        if (pattern.empty()) {
            continue;
        }
        storage.labels.push_back(pattern);
        storage.patterns.push_back(std::move(pattern));
    }

    storage.specs.reserve(storage.patterns.size());
    for (std::size_t index = 0; index < storage.patterns.size(); ++index) {
        storage.specs.push_back({
            storage.labels[index].c_str(),
            storage.patterns[index].c_str(),
        });
    }
    return storage;
}

class ComApartment {
public:
    ComApartment()
        : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE))
        , should_uninitialize_(SUCCEEDED(result_)) {}

    ~ComApartment() {
        if (should_uninitialize_) {
            CoUninitialize();
        }
    }

    [[nodiscard]] bool usable() const {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_ = E_FAIL;
    bool should_uninitialize_ = false;
};

FileDialogError file_dialog_error_from_hresult(HRESULT hr) {
    return hr == HRESULT_FROM_WIN32(ERROR_CANCELLED) ? FileDialogError::Cancelled
                                                     : FileDialogError::Failed;
}

EventLoop::TaskPoster file_dialog_task_poster() {
    if (auto* app = Application::instance(); app != nullptr) {
        return app->event_loop().task_poster("win32-file-dialog");
    }
    if (auto* loop = EventLoop::current(); loop != nullptr) {
        return loop->task_poster("win32-file-dialog");
    }
    return {};
}

template <typename Callback, typename Result>
void post_file_dialog_result(const EventLoop::TaskPoster& poster,
                             Callback callback,
                             Result result) {
    if (!poster) {
        return;
    }
    poster([callback = std::move(callback), result = std::move(result)]() mutable {
        if (callback) {
            callback(std::move(result));
        }
    });
}

OpenFileDialogResult show_modern_open_file_dialog(std::string_view title,
                                                  const std::vector<std::string>& filters,
                                                  HWND owner) {
    ComApartment apartment;
    if (!apartment.usable()) {
        return Unexpected(FileDialogError::Failed);
    }

    void* raw_dialog = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog, &raw_dialog);
    if (FAILED(hr)) {
        return Unexpected(FileDialogError::Failed);
    }
    ComPtr<IFileOpenDialog> dialog;
    dialog.Attach(static_cast<IFileOpenDialog*>(raw_dialog));

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        (void)dialog->SetOptions(options | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST |
                                 FOS_PATHMUSTEXIST);
    }

    auto wide_title = utf8_to_wide(title);
    if (!wide_title.empty()) {
        (void)dialog->SetTitle(wide_title.c_str());
    }

    auto filter_storage = build_file_dialog_filters(filters);
    if (!filter_storage.specs.empty()) {
        (void)dialog->SetFileTypes(static_cast<UINT>(filter_storage.specs.size()),
                                   filter_storage.specs.data());
        (void)dialog->SetFileTypeIndex(1);
    }

    // Owning the dialog to the app's active window keeps it app-modal, on top
    // of the application, and grouped with it on the taskbar.
    hr = dialog->Show(owner);
    if (FAILED(hr)) {
        return Unexpected(file_dialog_error_from_hresult(hr));
    }

    ComPtr<IShellItem> item;
    hr = dialog->GetResult(&item);
    if (FAILED(hr)) {
        return Unexpected(FileDialogError::Failed);
    }

    PWSTR path = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (FAILED(hr) || path == nullptr) {
        return Unexpected(FileDialogError::Failed);
    }

    auto result = wide_to_utf8(path);
    CoTaskMemFree(path);
    return result.empty() ? OpenFileDialogResult(Unexpected(FileDialogError::Failed))
                          : OpenFileDialogResult(std::move(result));
}

SaveFileDialogResult show_modern_save_file_dialog(const SaveFileDialogOptions& options,
                                                  HWND owner) {
    ComApartment apartment;
    if (!apartment.usable()) {
        return Unexpected(FileDialogError::Failed);
    }

    void* raw_dialog = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileSaveDialog, &raw_dialog);
    if (FAILED(hr)) {
        return Unexpected(FileDialogError::Failed);
    }
    ComPtr<IFileSaveDialog> dialog;
    dialog.Attach(static_cast<IFileSaveDialog*>(raw_dialog));

    DWORD dialog_options = 0;
    if (SUCCEEDED(dialog->GetOptions(&dialog_options))) {
        dialog_options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        if (options.confirm_overwrite) {
            dialog_options |= FOS_OVERWRITEPROMPT;
        }
        (void)dialog->SetOptions(dialog_options);
    }

    auto wide_title = utf8_to_wide(options.title);
    if (!wide_title.empty()) {
        (void)dialog->SetTitle(wide_title.c_str());
    }

    auto suggested_filename = utf8_to_wide(options.suggested_filename);
    if (!suggested_filename.empty()) {
        (void)dialog->SetFileName(suggested_filename.c_str());
    }

    auto filter_storage = build_file_dialog_filters(options.filters);
    if (!filter_storage.specs.empty()) {
        (void)dialog->SetFileTypes(static_cast<UINT>(filter_storage.specs.size()),
                                   filter_storage.specs.data());
        (void)dialog->SetFileTypeIndex(1);
    }

    auto default_extension = default_extension_for_filters(options.filters);
    if (!default_extension.empty()) {
        (void)dialog->SetDefaultExtension(default_extension.c_str());
    }

    hr = dialog->Show(owner);
    if (FAILED(hr)) {
        return Unexpected(file_dialog_error_from_hresult(hr));
    }

    ComPtr<IShellItem> item;
    hr = dialog->GetResult(&item);
    if (FAILED(hr)) {
        return Unexpected(FileDialogError::Failed);
    }

    PWSTR path = nullptr;
    hr = item->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (FAILED(hr) || path == nullptr) {
        return Unexpected(FileDialogError::Failed);
    }

    auto result = wide_to_utf8(path);
    CoTaskMemFree(path);
    return result.empty() ? SaveFileDialogResult(Unexpected(FileDialogError::Failed))
                          : SaveFileDialogResult(std::move(result));
}

HCURSOR cursor_for_shape(CursorShape shape) {
    LPCWSTR id = IDC_ARROW;
    switch (shape) {
    case CursorShape::Default:
        id = IDC_ARROW;
        break;
    case CursorShape::PointingHand:
        id = IDC_HAND;
        break;
    case CursorShape::IBeam:
        id = IDC_IBEAM;
        break;
    case CursorShape::ResizeLeftRight:
        id = IDC_SIZEWE;
        break;
    case CursorShape::ResizeUpDown:
        id = IDC_SIZENS;
        break;
    }
    return LoadCursorW(nullptr, id);
}

RECT logical_client_rect_for_window(Size logical_size,
                                    DWORD style,
                                    DWORD ex_style,
                                    float scale_factor) {
    const int pixel_width = std::max(
        1, static_cast<int>(std::lround(logical_size.width * std::max(scale_factor, 1.0F))));
    const int pixel_height = std::max(
        1, static_cast<int>(std::lround(logical_size.height * std::max(scale_factor, 1.0F))));
    RECT rect{0, 0, pixel_width, pixel_height};

    using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
    static const auto adjust_for_dpi = reinterpret_cast<AdjustWindowRectExForDpiFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
    if (adjust_for_dpi != nullptr) {
        adjust_for_dpi(&rect,
                       style,
                       FALSE,
                       ex_style,
                       static_cast<UINT>(std::lround(std::max(scale_factor, 1.0F) * 96.0F)));
    } else {
        AdjustWindowRectEx(&rect, style, FALSE, ex_style);
    }
    return rect;
}

// --- Native menu bar helpers -----------------------------------------------

/// Map a KeyCode to a single-character shortcut label (e.g. KeyCode::S -> "S").
/// Returns empty when the key has no useful textual accelerator (arrows, F-keys
/// handled separately below).
[[nodiscard]] std::wstring shortcut_label_for_key(KeyCode key) {
    switch (key) {
    case KeyCode::A:
        return L"A";
    case KeyCode::B:
        return L"B";
    case KeyCode::C:
        return L"C";
    case KeyCode::D:
        return L"D";
    case KeyCode::E:
        return L"E";
    case KeyCode::F:
        return L"F";
    case KeyCode::G:
        return L"G";
    case KeyCode::H:
        return L"H";
    case KeyCode::I:
        return L"I";
    case KeyCode::J:
        return L"J";
    case KeyCode::K:
        return L"K";
    case KeyCode::L:
        return L"L";
    case KeyCode::M:
        return L"M";
    case KeyCode::N:
        return L"N";
    case KeyCode::O:
        return L"O";
    case KeyCode::P:
        return L"P";
    case KeyCode::Q:
        return L"Q";
    case KeyCode::R:
        return L"R";
    case KeyCode::S:
        return L"S";
    case KeyCode::T:
        return L"T";
    case KeyCode::U:
        return L"U";
    case KeyCode::V:
        return L"V";
    case KeyCode::W:
        return L"W";
    case KeyCode::X:
        return L"X";
    case KeyCode::Y:
        return L"Y";
    case KeyCode::Z:
        return L"Z";
    case KeyCode::Num0:
        return L"0";
    case KeyCode::Num1:
        return L"1";
    case KeyCode::Num2:
        return L"2";
    case KeyCode::Num3:
        return L"3";
    case KeyCode::Num4:
        return L"4";
    case KeyCode::Num5:
        return L"5";
    case KeyCode::Num6:
        return L"6";
    case KeyCode::Num7:
        return L"7";
    case KeyCode::Num8:
        return L"8";
    case KeyCode::Num9:
        return L"9";
    case KeyCode::Return:
        return L"Enter";
    case KeyCode::Escape:
        return L"Esc";
    case KeyCode::Backspace:
        return L"Backspace";
    case KeyCode::Tab:
        return L"Tab";
    case KeyCode::Space:
        return L"Space";
    case KeyCode::Delete:
        return L"Delete";
    case KeyCode::Left:
        return L"Left";
    case KeyCode::Right:
        return L"Right";
    case KeyCode::Up:
        return L"Up";
    case KeyCode::Down:
        return L"Down";
    case KeyCode::Home:
        return L"Home";
    case KeyCode::End:
        return L"End";
    case KeyCode::PageUp:
        return L"PageUp";
    case KeyCode::PageDown:
        return L"PageDown";
    case KeyCode::Minus:
        return L"-";
    case KeyCode::Equals:
        return L"=";
    case KeyCode::LeftBracket:
        return L"[";
    case KeyCode::RightBracket:
        return L"]";
    case KeyCode::Backslash:
        return L"\\";
    case KeyCode::Semicolon:
        return L";";
    case KeyCode::Apostrophe:
        return L"'";
    case KeyCode::Comma:
        return L",";
    case KeyCode::Period:
        return L".";
    case KeyCode::Slash:
        return L"/";
    case KeyCode::F1:
        return L"F1";
    case KeyCode::F2:
        return L"F2";
    case KeyCode::F3:
        return L"F3";
    case KeyCode::F4:
        return L"F4";
    case KeyCode::F5:
        return L"F5";
    case KeyCode::F6:
        return L"F6";
    case KeyCode::F7:
        return L"F7";
    case KeyCode::F8:
        return L"F8";
    case KeyCode::F9:
        return L"F9";
    case KeyCode::F10:
        return L"F10";
    case KeyCode::F11:
        return L"F11";
    case KeyCode::F12:
        return L"F12";
    default:
        return {};
    }
}

/// Build a "Ctrl+Shift+S" style suffix ("\tCtrl+Shift+S") for a NativeMenuShortcut.
/// Win32 renders the part after the tab as the right-aligned accelerator hint.
/// Super (Win key) is omitted: it has no conventional menu-accelerator spelling on
/// Windows and would conflict with system shortcuts.
[[nodiscard]] std::wstring shortcut_suffix(const NativeMenuShortcut& shortcut) {
    std::wstring parts;
    if ((shortcut.modifiers & NativeMenuModifier::Ctrl) != NativeMenuModifier::None) {
        parts += L"Ctrl+";
    }
    if ((shortcut.modifiers & NativeMenuModifier::Alt) != NativeMenuModifier::None) {
        parts += L"Alt+";
    }
    if ((shortcut.modifiers & NativeMenuModifier::Shift) != NativeMenuModifier::None) {
        parts += L"Shift+";
    }
    const auto label = shortcut_label_for_key(shortcut.key);
    if (label.empty()) {
        return {};
    }
    return L"\t" + parts + label;
}

/// Standard Windows shortcut for a native menu role, used when the item
/// declares no explicit shortcut: the label hint and the accelerator table
/// both synthesize the conventional Ctrl+<key> spelling.
[[nodiscard]] std::optional<NativeMenuShortcut> role_shortcut_for(NativeMenuRole role) {
    NativeMenuShortcut shortcut{};
    shortcut.modifiers = NativeMenuModifier::Ctrl;
    switch (role) {
    case NativeMenuRole::Undo:
        shortcut.key = KeyCode::Z;
        break;
    case NativeMenuRole::Redo:
        shortcut.key = KeyCode::Y;
        break;
    case NativeMenuRole::Cut:
        shortcut.key = KeyCode::X;
        break;
    case NativeMenuRole::Copy:
        shortcut.key = KeyCode::C;
        break;
    case NativeMenuRole::Paste:
        shortcut.key = KeyCode::V;
        break;
    case NativeMenuRole::SelectAll:
        shortcut.key = KeyCode::A;
        break;
    case NativeMenuRole::Minimize:
    case NativeMenuRole::BringAllToFront:
    case NativeMenuRole::Custom:
    default:
        return std::nullopt;
    }
    return shortcut;
}

/// Whether a role has a Windows-side effect (synthetic shortcut keys or a
/// window action) when its item carries no action_name.
[[nodiscard]] bool role_dispatchable_on_windows(NativeMenuRole role) {
    return role == NativeMenuRole::Minimize || role_shortcut_for(role).has_value();
}

/// Recursively append NativeMenuItem entries to a popup HMENU. Each leaf with an
/// action_name gets a unique command id recorded in command_actions so WM_COMMAND
/// can dispatch it through the shared NativeMenuActionHandler. Role-only items are
/// recorded in command_roles instead, and every dispatchable leaf with a
/// representable shortcut also contributes an accelerator entry sharing the
/// same command id.
void build_win32_menu_popup(HMENU popup,
                            std::span<const NativeMenuItem> items,
                            UINT& next_command,
                            std::unordered_map<UINT, std::string>& command_actions,
                            std::unordered_map<UINT, NativeMenuRole>& command_roles,
                            std::vector<ACCEL>& accelerators) {
    for (const auto& item : items) {
        if (item.separator) {
            AppendMenuW(popup, MF_SEPARATOR, 0u, nullptr);
            continue;
        }
        if (!item.children.empty()) {
            const HMENU submenu = CreatePopupMenu();
            build_win32_menu_popup(
                submenu, item.children, next_command, command_actions, command_roles, accelerators);
            AppendMenuW(popup,
                        MF_POPUP | (item.enabled ? MF_ENABLED : MF_GRAYED),
                        reinterpret_cast<UINT_PTR>(submenu),
                        utf8_to_wide(item.label).c_str());
            continue;
        }
        const UINT command_id = next_command++;
        std::wstring label = utf8_to_wide(item.label);
        NativeMenuShortcut shortcut = item.shortcut.value_or(NativeMenuShortcut{});
        if (!item.shortcut.has_value()) {
            if (const auto role_shortcut = role_shortcut_for(item.native_role)) {
                shortcut = *role_shortcut;
            }
        }
        if (shortcut.key != KeyCode::Unknown) {
            label += shortcut_suffix(shortcut);
        }
        const bool dispatchable =
            !item.action_name.empty() || role_dispatchable_on_windows(item.native_role);
        const UINT flags = MF_STRING | (item.enabled && dispatchable ? MF_ENABLED : MF_GRAYED);
        AppendMenuW(popup, flags, command_id, label.c_str());
        if (dispatchable) {
            if (!item.action_name.empty()) {
                command_actions.emplace(command_id, item.action_name);
            } else {
                command_roles.emplace(command_id, item.native_role);
            }
        }
        ACCEL accelerator{};
        if (item.enabled && dispatchable &&
            detail::win32_accel_for_shortcut(shortcut, command_id, &accelerator)) {
            accelerators.push_back(accelerator);
        }
    }
}

/// Build a menu bar (HMENU) from the app-global NativeMenu model. First command
/// id is 1 (0 is reserved by Win32).
[[nodiscard]] HMENU build_win32_menu_bar(std::span<const NativeMenu> menus,
                                         std::unordered_map<UINT, std::string>& command_actions,
                                         std::unordered_map<UINT, NativeMenuRole>& command_roles,
                                         std::vector<ACCEL>& accelerators) {
    command_actions.clear();
    command_roles.clear();
    const HMENU bar = CreateMenu();
    UINT next_command = 1;
    for (const auto& menu : menus) {
        const HMENU popup = CreatePopupMenu();
        build_win32_menu_popup(
            popup, menu.items, next_command, command_actions, command_roles, accelerators);
        AppendMenuW(bar,
                    MF_POPUP | MF_STRING,
                    reinterpret_cast<UINT_PTR>(popup),
                    utf8_to_wide(menu.title).c_str());
    }
    return bar;
}

// --- OLE drop target (receive side) ----------------------------------------

/// Win32 clipboard/DnD effect flags for a NodalKit DragOperation.
[[nodiscard]] DWORD drop_effect_for_operation(DragOperation operation) {
    switch (operation) {
    case DragOperation::Copy:
        return DROPEFFECT_COPY;
    case DragOperation::Move:
        return DROPEFFECT_MOVE;
    case DragOperation::Link:
        return DROPEFFECT_LINK;
    case DragOperation::None:
    default:
        return DROPEFFECT_NONE;
    }
}

/// NodalKit DragOperation for the Win32 source-effect bitmask, choosing the most
/// "destructive" allowed operation as the requested one.
[[nodiscard]] DragOperation operation_from_effect(DWORD effect) {
    if ((effect & DROPEFFECT_COPY) != 0) {
        return DragOperation::Copy;
    }
    if ((effect & DROPEFFECT_MOVE) != 0) {
        return DragOperation::Move;
    }
    if ((effect & DROPEFFECT_LINK) != 0) {
        return DragOperation::Link;
    }
    return DragOperation::None;
}

/// Parse a CF_HDROP blob (the `hGlobal` payload of an IDataObject) into file
/// paths. Returns null if the data is not a file drop.
[[nodiscard]] std::shared_ptr<const DragPayload>
payload_from_data_object(IDataObject* data_object) {
    if (data_object == nullptr) {
        return nullptr;
    }
    // Prefer CF_HDROP (files), then fall back to CF_UNICODETEXT (plain text).
    FORMATETC hdrop_format{
        .cfFormat = CF_HDROP,
        .ptd = nullptr,
        .dwAspect = DVASPECT_CONTENT,
        .lindex = -1,
        .tymed = TYMED_HGLOBAL,
    };
    STGMEDIUM medium{};
    if (SUCCEEDED(data_object->GetData(&hdrop_format, &medium)) && medium.hGlobal != nullptr) {
        // The CF_HDROP global is itself a valid HDROP (a DROPFILES header followed
        // by the path list). DragQueryFileW is the supported way to enumerate it,
        // avoiding any dependency on the DROPFILES struct layout across SDKs.
        const auto drop = static_cast<HDROP>(medium.hGlobal);
        std::shared_ptr<const DragPayload> payload;
        const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        if (count != 0) {
            std::vector<std::filesystem::path> files;
            files.reserve(count);
            for (UINT i = 0; i < count; ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                if (length == 0) {
                    continue;
                }
                std::wstring path(length, L'\0');
                DragQueryFileW(drop, i, path.data(), length + 1);
                files.emplace_back(std::move(path));
            }
            payload = std::make_shared<DragPayload>(DragPayload::from_files(std::move(files)));
        }
        ReleaseStgMedium(&medium);
        if (payload != nullptr) {
            return payload;
        }
    }

    FORMATETC text_format{
        .cfFormat = CF_UNICODETEXT,
        .ptd = nullptr,
        .dwAspect = DVASPECT_CONTENT,
        .lindex = -1,
        .tymed = TYMED_HGLOBAL,
    };
    if (SUCCEEDED(data_object->GetData(&text_format, &medium)) && medium.hGlobal != nullptr) {
        auto text = detail::read_global_unicode_text(medium.hGlobal);
        ReleaseStgMedium(&medium);
        return text ? std::make_shared<DragPayload>(DragPayload::from_text(std::move(*text)))
                    : nullptr;
    }
    return nullptr;
}

class Win32Surface; // forward

/// Hand-rolled IDropTarget that translates OLE drag notifications into
/// NodalKit DragDropEvents delivered through Win32Surface::owner(). Reference-
/// counted so ComPtr can hold it; the surface holds the owning reference for the
/// window's lifetime and clears the back-pointer before destruction.
class Win32DropTarget final : public IDropTarget {
public:
    explicit Win32DropTarget(Win32Surface* surface) : surface_(surface) {}

    void detach() { surface_ = nullptr; }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *out = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_count_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data_object,
                                        DWORD key_state,
                                        POINTL point,
                                        DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD key_state, POINTL point, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data_object,
                                   DWORD key_state,
                                   POINTL point,
                                   DWORD* effect) override;

private:
    /// Build a DragDropEvent of `type` from the data object + cursor position,
    /// dispatch it, and write the resulting effect. Returns S_OK even when no
    /// payload is available (effect just becomes DROPEFFECT_NONE).
    HRESULT dispatch(IDataObject* data_object,
                     DragDropEventType type,
                     POINTL point,
                     DWORD source_effect,
                     DWORD* out_effect);

    std::atomic<ULONG> ref_count_{1};
    Win32Surface* surface_;
    // Payload extracted on DragEnter and reused for Over/Leave/Drop, matching the
    // macOS backend which re-resolves per event but benefits from the same value.
    std::shared_ptr<const DragPayload> enter_payload_;
    DragOperation enter_operation_ = DragOperation::None;
};

/// Accelerator tables by window, consulted by the backend message loop before
/// TranslateMessage so accelerator keys never reach the toolkit as raw key
/// events (which would double-activate actions in apps that also install
/// shortcut bindings).
std::unordered_map<HWND, HACCEL>& accelerator_table_registry() {
    static std::unordered_map<HWND, HACCEL> registry;
    return registry;
}

/// Minimal IDropSource: keep dragging while a button stays pressed, cancel on
/// Escape, and let OLE draw the default drag cursors.
class Win32DropSource final : public IDropSource {
public:
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *out = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_count_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IDropSource
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape_pressed, DWORD key_state) override {
        if (escape_pressed != FALSE) {
            return DRAGDROP_S_CANCEL;
        }
        if ((key_state & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON)) == 0) {
            return DRAGDROP_S_DROP;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD /*effect*/) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    std::atomic<ULONG> ref_count_{1};
};

class Win32Surface final : public NativeSurface {
public:
    Win32Surface(HINSTANCE instance, const WindowConfig& config, Window& owner);
    ~Win32Surface() override;

    void show() override;
    void hide() override;
    void set_title(std::string_view title) override;
    void resize(int width, int height) override;
    [[nodiscard]] Size size() const override;
    [[nodiscard]] float scale_factor() const override;
    [[nodiscard]] Size framebuffer_size() const override;
    [[nodiscard]] RendererBackendSupport renderer_backend_support() const override;
    void present(const uint8_t* rgba,
                 int width,
                 int height,
                 std::span<const Rect> damage_regions) override;
    void set_fullscreen(bool fullscreen) override;
    [[nodiscard]] bool is_fullscreen() const override;
    void minimize() override;
    void toggle_maximize() override;
    [[nodiscard]] bool is_maximized() const override;
    [[nodiscard]] NativeWindowHandle native_handle() const override;
    [[nodiscard]] NativeWindowHandle native_display_handle() const override;
    void set_cursor_shape(CursorShape shape) override;
    void text_input_state_changed() override;
    [[nodiscard]] bool start_native_drag(const DragPayload& payload,
                                         DragOperation operation) override;
    [[nodiscard]] HWND hwnd() const;

    /// Build a Win32 menu bar from the app-global menu model and attach it to the
    /// window. The handler pointer points back into Win32Backend::Impl and stays
    /// valid for the backend's lifetime. Called once at surface creation; safe to
    /// call again to replace an existing menu bar. Also (re)builds the HACCEL
    /// accelerator table for the menu's dispatchable shortcuts.
    void apply_native_menu(std::span<const NativeMenu> menus,
                           const NativeMenuActionHandler* handler);

    /// Deliver a drag & drop event to the widget tree. Called by Win32DropTarget
    /// with external=true (drops from outside the process, e.g. Explorer).
    [[nodiscard]] DragOperation dispatch_drop_event(const DragDropEvent& event);

    /// Logical position (window coordinates, divided by scale factor) for a
    /// screen-space POINTL. Used by the drop target to fill event.position.
    [[nodiscard]] Point logical_point_for_screen(POINTL screen) const;

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

private:
    void update_metrics(UINT dpi_override = 0);
    void dispatch_metric_events(Size previous_logical_size, float previous_scale_factor);
    void blit_back_buffer(HDC dc);
    void dispatch_mouse_event(MouseEvent event);
    void dispatch_key_event(KeyEvent event);
    void dispatch_text_utf16(wchar_t code_unit);
    void dispatch_native_menu_role(NativeMenuRole role);
    void handle_ime_composition(LPARAM lparam);
    void position_ime_windows();
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);

    HINSTANCE instance_ = nullptr;
    Window& owner_;
    HWND hwnd_ = nullptr;
    DWORD style_ = 0;
    DWORD ex_style_ = 0;
    DWORD windowed_style_ = 0;
    RECT windowed_rect_{};
    Size logical_size_{};
    Size physical_size_{};
    float scale_factor_ = 1.0F;
    int buffer_width_ = 0;
    int buffer_height_ = 0;
    std::vector<uint8_t> back_buffer_;
    CursorShape current_cursor_ = CursorShape::Default;
    bool fullscreen_ = false;
    bool tracking_mouse_ = false;
    bool handling_dpi_change_ = false;
    wchar_t pending_high_surrogate_ = 0;
    // Multi-click state for button events; the count computed at Press is
    // mirrored on the matching Release.
    detail::Win32ClickCounter click_counter_;
    int press_click_count_ = 1;
    // True between WM_IME_STARTCOMPOSITION and WM_IME_ENDCOMPOSITION.
    bool ime_composing_ = false;
    // Native menu bar state. Command ids start at 1 (0 is reserved by Win32).
    HMENU menu_bar_ = nullptr;
    std::unordered_map<UINT, std::string> menu_command_actions_;
    std::unordered_map<UINT, NativeMenuRole> menu_command_roles_;
    const NativeMenuActionHandler* menu_handler_ = nullptr;
    // HACCEL table built alongside the menu bar; registered in the backend's
    // per-window registry so the message loop can translate accelerator keys.
    HACCEL accelerator_table_ = nullptr;
    // OLE drop target registered on this window. Owned via refcount; we hold one
    // strong reference and revoke it in the destructor before releasing.
    ComPtr<Win32DropTarget> drop_target_;
    // UI Automation bridge, created lazily on the first UIA WM_GETOBJECT.
    std::unique_ptr<detail::Win32UiaBridge> uia_;
};

Win32Surface::Win32Surface(HINSTANCE instance, const WindowConfig& config, Window& owner)
    : instance_(instance), owner_(owner) {
    style_ = config.decorated ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    if (!config.resizable) {
        style_ &= ~WS_THICKFRAME;
        style_ &= ~WS_MAXIMIZEBOX;
    }
    ex_style_ = WS_EX_APPWINDOW;

    const RECT window_rect =
        logical_client_rect_for_window({static_cast<float>(std::max(1, config.width)),
                                        static_cast<float>(std::max(1, config.height))},
                                       style_,
                                       ex_style_,
                                       current_system_scale());

    hwnd_ = CreateWindowExW(ex_style_,
                            kWindowClassName,
                            utf8_to_wide(config.title).c_str(),
                            style_,
                            CW_USEDEFAULT,
                            CW_USEDEFAULT,
                            window_rect.right - window_rect.left,
                            window_rect.bottom - window_rect.top,
                            nullptr,
                            nullptr,
                            instance_,
                            this);
    if (hwnd_ != nullptr) {
        update_metrics();
        // Match the native frame to the system color scheme up front; the
        // WM_SETTINGCHANGE handler re-applies it when the user switches themes.
        apply_color_scheme_chrome(hwnd_, query_color_scheme());
        // Register an OLE drop target so external drags (e.g. files from
        // Explorer) reach the widget tree. RegisterDragDrop requires OLE to be
        // initialized on this thread; if it isn't (OleInitialize failed in the
        // backend) we skip registration and DnD simply stays disabled.
        auto* target = new Win32DropTarget(this);
        drop_target_ = target; // surface holds the owning reference
        const HRESULT reg_hr = RegisterDragDrop(hwnd_, target);
        if (FAILED(reg_hr)) {
            NK_LOG_WARN("Win32Backend", "RegisterDragDrop failed; drops disabled for window");
            // Revoke isn't needed; nothing was registered. Keep drop_target_ alive
            // so a later retry path could re-register; it releases with the surface.
        }
    }
}

Win32Surface::~Win32Surface() {
    if (hwnd_ != nullptr) {
        // Detach the surface from the window procedure before destroying the HWND.
        // DestroyWindow synchronously delivers WM_KILLFOCUS/WM_DESTROY, and forwarding
        // those into the owning Window while it is being torn down would re-enter event
        // dispatch on half-destroyed widgets and the text shaper. With the back pointer
        // cleared, window_proc falls through to DefWindowProcW during teardown.
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        if (menu_bar_ != nullptr) {
            SetMenu(hwnd_, nullptr);
            DestroyMenu(menu_bar_);
            menu_bar_ = nullptr;
        }
        if (accelerator_table_ != nullptr) {
            accelerator_table_registry().erase(hwnd_);
            DestroyAcceleratorTable(accelerator_table_);
            accelerator_table_ = nullptr;
        }
        // Detach + revoke the drop target before the HWND and widget tree go
        // away: RevokeDragDrop blocks until in-flight OLE callbacks return, and
        // clearing the back-pointer stops any late callback from dereferencing us.
        if (drop_target_ != nullptr) {
            drop_target_->detach();
            RevokeDragDrop(hwnd_);
            drop_target_.Reset();
        }
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        // The bridge was disconnected from WM_DESTROY; release it with the
        // surface so providers never outlive the window they describe.
        uia_.reset();
    }
}

void Win32Surface::show() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
    }
}

void Win32Surface::hide() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void Win32Surface::set_title(std::string_view title) {
    if (hwnd_ != nullptr) {
        SetWindowTextW(hwnd_, utf8_to_wide(title).c_str());
    }
}

void Win32Surface::resize(int width, int height) {
    if (hwnd_ == nullptr) {
        return;
    }
    const RECT rect = logical_client_rect_for_window(
        {static_cast<float>(std::max(1, width)), static_cast<float>(std::max(1, height))},
        style_,
        ex_style_,
        scale_factor_);
    SetWindowPos(hwnd_,
                 nullptr,
                 0,
                 0,
                 rect.right - rect.left,
                 rect.bottom - rect.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    update_metrics();
}

Size Win32Surface::size() const {
    return logical_size_;
}

float Win32Surface::scale_factor() const {
    return scale_factor_;
}

Size Win32Surface::framebuffer_size() const {
    return physical_size_;
}

RendererBackendSupport Win32Surface::renderer_backend_support() const {
    RendererBackendSupport support;
    support.software = true;
    support.d3d11 = true;
    support.d3d12 = true;
#if defined(NK_HAVE_VULKAN)
    support.vulkan = true;
#endif
    return support;
}

void Win32Surface::present(const uint8_t* rgba,
                           int width,
                           int height,
                           std::span<const Rect> /*damage_regions*/) {
    if (hwnd_ == nullptr || rgba == nullptr || width <= 0 || height <= 0) {
        return;
    }
    if (buffer_width_ != width || buffer_height_ != height) {
        buffer_width_ = width;
        buffer_height_ = height;
        back_buffer_.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto index = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(x)) *
                               4;
            back_buffer_[index + 0] = rgba[index + 2];
            back_buffer_[index + 1] = rgba[index + 1];
            back_buffer_[index + 2] = rgba[index + 0];
            back_buffer_[index + 3] = rgba[index + 3];
        }
    }

    HDC dc = GetDC(hwnd_);
    if (dc != nullptr) {
        blit_back_buffer(dc);
        ReleaseDC(hwnd_, dc);
    }
}

void Win32Surface::set_fullscreen(bool fullscreen) {
    if (hwnd_ == nullptr || fullscreen_ == fullscreen) {
        return;
    }

    fullscreen_ = fullscreen;
    if (fullscreen_) {
        GetWindowRect(hwnd_, &windowed_rect_);
        windowed_style_ = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));

        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor_info);

        SetWindowLongPtrW(
            hwnd_, GWL_STYLE, static_cast<LONG_PTR>(windowed_style_ & ~WS_OVERLAPPEDWINDOW));
        SetWindowPos(hwnd_,
                     HWND_TOP,
                     monitor_info.rcMonitor.left,
                     monitor_info.rcMonitor.top,
                     monitor_info.rcMonitor.right - monitor_info.rcMonitor.left,
                     monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowLongPtrW(hwnd_, GWL_STYLE, static_cast<LONG_PTR>(windowed_style_));
        SetWindowPos(hwnd_,
                     nullptr,
                     windowed_rect_.left,
                     windowed_rect_.top,
                     windowed_rect_.right - windowed_rect_.left,
                     windowed_rect_.bottom - windowed_rect_.top,
                     SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    update_metrics();
}

bool Win32Surface::is_fullscreen() const {
    return fullscreen_;
}

void Win32Surface::minimize() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, SW_MINIMIZE);
    }
}

void Win32Surface::toggle_maximize() {
    if (hwnd_ != nullptr) {
        ShowWindow(hwnd_, IsZoomed(hwnd_) != FALSE ? SW_RESTORE : SW_MAXIMIZE);
    }
}

bool Win32Surface::is_maximized() const {
    return hwnd_ != nullptr && IsZoomed(hwnd_) != FALSE;
}

NativeWindowHandle Win32Surface::native_handle() const {
    return reinterpret_cast<NativeWindowHandle>(hwnd_);
}

NativeWindowHandle Win32Surface::native_display_handle() const {
    return reinterpret_cast<NativeWindowHandle>(instance_);
}

void Win32Surface::set_cursor_shape(CursorShape shape) {
    current_cursor_ = shape;
    if (hwnd_ != nullptr) {
        SetCursor(cursor_for_shape(shape));
    }
}

HWND Win32Surface::hwnd() const {
    return hwnd_;
}

void Win32Surface::text_input_state_changed() {
    position_ime_windows();
}

void Win32Surface::position_ime_windows() {
    if (hwnd_ == nullptr) {
        return;
    }
    // Anchor the system composition and candidate windows to the focused
    // editor's caret so IME UI tracks the text being composed.
    const auto state = owner_.current_text_input_state();
    if (!state.has_value() || state->caret_rect.width <= 0.0F || state->caret_rect.height <= 0.0F) {
        return;
    }
    const float scale = scale_factor_ > 0.0F ? scale_factor_ : 1.0F;
    POINT caret{
        static_cast<LONG>(std::lround(state->caret_rect.right() * scale)),
        static_cast<LONG>(std::lround(state->caret_rect.bottom() * scale)),
    };
    ClientToScreen(hwnd_, &caret);

    HIMC context = ImmGetContext(hwnd_);
    if (context == nullptr) {
        return;
    }
    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = caret;
    (void)ImmSetCompositionWindow(context, &composition);
    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = caret;
    (void)ImmSetCandidateWindow(context, &candidate);
    ImmReleaseContext(hwnd_, context);
}

bool Win32Surface::start_native_drag(const DragPayload& payload, DragOperation operation) {
    if (hwnd_ == nullptr) {
        return false;
    }
    auto* data_object = detail::win32_make_drag_data_object(payload);
    if (data_object == nullptr) {
        // In-process application data cannot cross the OLE boundary; the
        // caller falls back to the synthetic in-process drag.
        return false;
    }
    // DoDragDrop runs a modal OLE loop: the user drops by releasing the button
    // and cancels with Escape. Windows under the cursor — including this
    // process's own — receive the normal OLE drop-target events.
    Win32DropSource drop_source;
    const DWORD requested = drop_effect_for_operation(operation);
    const DWORD allowed =
        requested != DROPEFFECT_NONE ? (requested | DROPEFFECT_COPY) : DROPEFFECT_COPY;
    DWORD final_effect = DROPEFFECT_NONE;
    (void)DoDragDrop(data_object, &drop_source, allowed, &final_effect);
    data_object->Release();
    return true;
}

void Win32Surface::dispatch_native_menu_role(NativeMenuRole role) {
    // Role items without an action_name route to the platform's active
    // responder: edit roles replay their conventional Ctrl+<key> shortcuts as
    // synthetic key events into the focused widget (the same channel the
    // macOS backend uses for native roles); Minimize acts on the window.
    if (const auto shortcut = role_shortcut_for(role); shortcut.has_value()) {
        owner_.dispatch_key_event(
            {.type = KeyEvent::Type::Press, .key = shortcut->key, .modifiers = Modifiers::Ctrl});
        owner_.dispatch_key_event(
            {.type = KeyEvent::Type::Release, .key = shortcut->key, .modifiers = Modifiers::Ctrl});
        return;
    }
    if (role == NativeMenuRole::Minimize) {
        minimize();
    }
}

void Win32Surface::apply_native_menu(std::span<const NativeMenu> menus,
                                     const NativeMenuActionHandler* handler) {
    if (hwnd_ == nullptr) {
        return;
    }
    if (menu_bar_ != nullptr) {
        // Replacing the menu bar: destroy the old one and rebuild from the model.
        SetMenu(hwnd_, nullptr);
        DestroyMenu(menu_bar_);
        menu_bar_ = nullptr;
    }
    menu_command_actions_.clear();
    menu_command_roles_.clear();
    menu_handler_ = handler;
    if (accelerator_table_ != nullptr) {
        accelerator_table_registry().erase(hwnd_);
        DestroyAcceleratorTable(accelerator_table_);
        accelerator_table_ = nullptr;
    }
    if (menus.empty()) {
        DrawMenuBar(hwnd_);
        return;
    }
    std::vector<ACCEL> accelerators;
    menu_bar_ =
        build_win32_menu_bar(menus, menu_command_actions_, menu_command_roles_, accelerators);
    SetMenu(hwnd_, menu_bar_);
    DrawMenuBar(hwnd_);
    if (!accelerators.empty()) {
        accelerator_table_ =
            CreateAcceleratorTableW(accelerators.data(), static_cast<int>(accelerators.size()));
        if (accelerator_table_ != nullptr) {
            accelerator_table_registry()[hwnd_] = accelerator_table_;
        }
    }
}

DragOperation Win32Surface::dispatch_drop_event(const DragDropEvent& event) {
    return owner_.dispatch_drag_drop_event(event);
}

Point Win32Surface::logical_point_for_screen(POINTL screen) const {
    if (hwnd_ == nullptr) {
        return {0.0F, 0.0F};
    }
    POINT client{screen.x, screen.y};
    ScreenToClient(hwnd_, &client);
    const float scale = scale_factor_ > 0.0F ? scale_factor_ : 1.0F;
    return {static_cast<float>(client.x) / scale, static_cast<float>(client.y) / scale};
}

// --- Win32DropTarget (IDropTarget implementation) --------------------------
// Defined here, after Win32Surface is complete, so surface_->dispatch_drop_event
// and surface_->logical_point_for_screen resolve.

HRESULT Win32DropTarget::dispatch(IDataObject* data_object,
                                  DragDropEventType type,
                                  POINTL point,
                                  DWORD source_effect,
                                  DWORD* out_effect) {
    if (out_effect == nullptr) {
        return E_POINTER;
    }
    if (surface_ == nullptr) {
        *out_effect = DROPEFFECT_NONE;
        return S_OK;
    }
    auto payload = payload_from_data_object(data_object);
    DragDropEvent event{
        .type = type,
        .position = surface_->logical_point_for_screen(point),
        .payload = payload,
        .requested_operation = operation_from_effect(source_effect),
        .accepted_operation = DragOperation::None,
        .external = true,
    };
    if (payload == nullptr) {
        *out_effect = DROPEFFECT_NONE;
        return S_OK;
    }
    const DragOperation accepted = surface_->dispatch_drop_event(event);
    *out_effect = drop_effect_for_operation(accepted);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32DropTarget::DragEnter(IDataObject* data_object,
                                                     DWORD /*key_state*/,
                                                     POINTL point,
                                                     DWORD* effect) {
    if (effect == nullptr) {
        return E_POINTER;
    }
    enter_payload_ = payload_from_data_object(data_object);
    if (enter_payload_ == nullptr) {
        enter_operation_ = DragOperation::None;
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    // Re-resolve through dispatch() so the widget tree can accept/reject.
    return dispatch(data_object, DragDropEventType::Enter, point, *effect, effect);
}

HRESULT STDMETHODCALLTYPE Win32DropTarget::DragOver(DWORD /*key_state*/,
                                                    POINTL point,
                                                    DWORD* effect) {
    if (effect == nullptr) {
        return E_POINTER;
    }
    if (surface_ == nullptr || enter_payload_ == nullptr) {
        *effect = DROPEFFECT_NONE;
        return S_OK;
    }
    DragDropEvent event{
        .type = DragDropEventType::Motion,
        .position = surface_->logical_point_for_screen(point),
        .payload = enter_payload_,
        .requested_operation = operation_from_effect(*effect),
        .accepted_operation = DragOperation::None,
        .external = true,
    };
    const DragOperation accepted = surface_->dispatch_drop_event(event);
    *effect = drop_effect_for_operation(accepted);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32DropTarget::DragLeave() {
    if (surface_ == nullptr || enter_payload_ == nullptr) {
        enter_payload_.reset();
        enter_operation_ = DragOperation::None;
        return S_OK;
    }
    DragDropEvent event{
        .type = DragDropEventType::Leave,
        .position = {},
        .payload = enter_payload_,
        .requested_operation = enter_operation_,
        .accepted_operation = DragOperation::None,
        .external = true,
    };
    (void)surface_->dispatch_drop_event(event);
    enter_payload_.reset();
    enter_operation_ = DragOperation::None;
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32DropTarget::Drop(IDataObject* data_object,
                                                DWORD /*key_state*/,
                                                POINTL point,
                                                DWORD* effect) {
    if (effect == nullptr) {
        return E_POINTER;
    }
    // Use the freshly-resolved payload for the drop so an internal IDataObject
    // round-trip carries the most current data.
    const HRESULT hr = dispatch(data_object, DragDropEventType::Drop, point, *effect, effect);
    enter_payload_.reset();
    enter_operation_ = DragOperation::None;
    return hr;
}

// --- OLE drag source ---------------------------------------------------------

/// State carried by the hidden preferences-watch window (GWLP_USERDATA). Owned
/// by Win32Backend::Impl; pure data so the window procedure needs no access to
/// the backend itself.
struct PreferencesWatch {
    SystemPreferencesObserver observer;
    std::optional<SystemPreferences> last_observed;
};

/// Window procedure for the hidden top-level window that receives system
/// setting broadcasts. A message-only (HWND_MESSAGE) window would NOT receive
/// the WM_SETTINGCHANGE broadcast, so this must stay a real top-level window
/// that is simply never shown.
LRESULT CALLBACK preferences_window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(
            hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create_struct->lpCreateParams));
    }
    auto* watch = reinterpret_cast<PreferencesWatch*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (watch == nullptr) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    if (message == WM_SETTINGCHANGE || message == kWmThemeChange) {
        // Broadcasts arrive for many unrelated settings; requery and only
        // notify when something the toolkit tracks actually changed.
        const SystemPreferences latest = query_system_preferences();
        if (!watch->last_observed.has_value() || latest != *watch->last_observed) {
            watch->last_observed = latest;
            if (watch->observer) {
                watch->observer(latest);
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

/// Global allocation holding a double-NUL-terminated wide path list behind a
/// DROPFILES header — the CF_HDROP layout DragQueryFileW consumes.
[[nodiscard]] HGLOBAL global_for_file_list(const std::vector<std::filesystem::path>& files) {
    std::size_t chars = 1; // final terminating NUL
    for (const auto& file : files) {
        chars += file.native().size() + 1;
    }
    const std::size_t bytes = sizeof(LocalDropFiles) + chars * sizeof(wchar_t);
    HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (storage == nullptr) {
        return nullptr;
    }
    auto* drop_files = static_cast<LocalDropFiles*>(GlobalLock(storage));
    if (drop_files == nullptr) {
        GlobalFree(storage);
        return nullptr;
    }
    drop_files->pFiles = sizeof(LocalDropFiles);
    drop_files->pt = {};
    drop_files->fNC = FALSE;
    drop_files->fWide = TRUE;
    auto* out = reinterpret_cast<wchar_t*>(reinterpret_cast<std::byte*>(drop_files) +
                                           sizeof(LocalDropFiles));
    for (const auto& file : files) {
        const auto& wide = file.native();
        std::wmemcpy(out, wide.c_str(), wide.size());
        out += wide.size();
        *out++ = L'\0';
    }
    *out = L'\0';
    GlobalUnlock(storage);
    return storage;
}

/// Global allocation holding a NUL-terminated wide string (CF_UNICODETEXT).
[[nodiscard]] HGLOBAL global_for_wide_text(std::wstring_view text) {
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (storage == nullptr) {
        return nullptr;
    }
    auto* destination = static_cast<wchar_t*>(GlobalLock(storage));
    if (destination == nullptr) {
        GlobalFree(storage);
        return nullptr;
    }
    std::copy(text.begin(), text.end(), destination);
    destination[text.size()] = L'\0';
    GlobalUnlock(storage);
    return storage;
}

/// Minimal IEnumFORMATETC over the static format list a Win32DragDataObject
/// offers (DATADIR_GET direction only).
class Win32EnumFormatEtc final : public IEnumFORMATETC {
public:
    explicit Win32EnumFormatEtc(std::vector<FORMATETC> formats) : formats_(std::move(formats)) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) {
            *out = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_count_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IEnumFORMATETC
    HRESULT STDMETHODCALLTYPE Next(ULONG count, FORMATETC* out, ULONG* fetched) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        ULONG copied = 0;
        while (copied < count && index_ < formats_.size()) {
            out[copied++] = formats_[index_++];
        }
        if (fetched != nullptr) {
            *fetched = copied;
        }
        return copied == count ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override {
        index_ = std::min(index_ + static_cast<std::size_t>(count), formats_.size());
        return index_ < formats_.size() || count == 0 ? S_OK : S_FALSE;
    }

    HRESULT STDMETHODCALLTYPE Reset() override {
        index_ = 0;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Clone(IEnumFORMATETC** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        auto* clone = new (std::nothrow) Win32EnumFormatEtc(formats_);
        if (clone == nullptr) {
            return E_OUTOFMEMORY;
        }
        clone->index_ = index_;
        clone->AddRef();
        *out = clone;
        return S_OK;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    std::vector<FORMATETC> formats_;
    std::size_t index_ = 0;
};

/// Hand-rolled read-only IDataObject for DoDragDrop: offers CF_HDROP for file
/// payloads and CF_UNICODETEXT for text payloads, mirroring the formats the
/// receive side (payload_from_data_object) understands.
class Win32DragDataObject final : public IDataObject {
public:
    explicit Win32DragDataObject(const DragPayload& payload)
        : text_(utf8_to_wide(payload.text)), files_(payload.files) {
        if (!files_.empty()) {
            formats_.push_back({
                .cfFormat = CF_HDROP,
                .ptd = nullptr,
                .dwAspect = DVASPECT_CONTENT,
                .lindex = -1,
                .tymed = TYMED_HGLOBAL,
            });
        }
        if (!text_.empty()) {
            formats_.push_back({
                .cfFormat = CF_UNICODETEXT,
                .ptd = nullptr,
                .dwAspect = DVASPECT_CONTENT,
                .lindex = -1,
                .tymed = TYMED_HGLOBAL,
            });
        }
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *out = static_cast<IUnknown*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_count_; }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG count = --ref_count_;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IDataObject
    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
        if (format == nullptr || medium == nullptr) {
            return E_POINTER;
        }
        if ((format->tymed & TYMED_HGLOBAL) == 0 || format->lindex != -1) {
            return DV_E_TYMED;
        }
        if (format->cfFormat == CF_HDROP && !files_.empty()) {
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = global_for_file_list(files_);
            medium->pUnkForRelease = nullptr;
            return medium->hGlobal != nullptr ? S_OK : E_OUTOFMEMORY;
        }
        if (format->cfFormat == CF_UNICODETEXT && !text_.empty()) {
            medium->tymed = TYMED_HGLOBAL;
            medium->hGlobal = global_for_wide_text(text_);
            medium->pUnkForRelease = nullptr;
            return medium->hGlobal != nullptr ? S_OK : E_OUTOFMEMORY;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC* /*format*/, STGMEDIUM* /*medium*/) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
        if (format == nullptr || (format->tymed & TYMED_HGLOBAL) == 0) {
            return DV_E_TYMED;
        }
        for (const auto& offered : formats_) {
            if (offered.cfFormat == format->cfFormat) {
                return S_OK;
            }
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC* /*format*/,
                                                    FORMATETC* out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        out->ptd = nullptr;
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE SetData(FORMATETC* /*format*/,
                                      STGMEDIUM* /*medium*/,
                                      BOOL /*release*/) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) override {
        if (out == nullptr) {
            return E_POINTER;
        }
        if (direction != DATADIR_GET) {
            *out = nullptr;
            return E_NOTIMPL;
        }
        auto* enumerator = new (std::nothrow) Win32EnumFormatEtc(formats_);
        if (enumerator == nullptr) {
            return E_OUTOFMEMORY;
        }
        *out = enumerator;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC* /*format*/,
                                      DWORD /*advf*/,
                                      IAdviseSink* /*sink*/,
                                      DWORD* /*connection*/) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD /*connection*/) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA** out) override {
        if (out != nullptr) {
            *out = nullptr;
        }
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    std::atomic<ULONG> ref_count_{1};
    std::wstring text_;
    std::vector<std::filesystem::path> files_;
    std::vector<FORMATETC> formats_;
};

void Win32Surface::update_metrics(UINT dpi_override) {
    if (hwnd_ == nullptr) {
        return;
    }
    scale_factor_ =
        dpi_override != 0 ? scale_factor_from_dpi(dpi_override) : query_dpi_scale(hwnd_);
    RECT client_rect{};
    GetClientRect(hwnd_, &client_rect);
    physical_size_ = {static_cast<float>(std::max<LONG>(0, client_rect.right - client_rect.left)),
                      static_cast<float>(std::max<LONG>(0, client_rect.bottom - client_rect.top))};
    logical_size_ = {
        scale_factor_ > 0.0F ? physical_size_.width / scale_factor_ : physical_size_.width,
        scale_factor_ > 0.0F ? physical_size_.height / scale_factor_ : physical_size_.height};
}

void Win32Surface::dispatch_metric_events(Size previous_logical_size, float previous_scale_factor) {
    if (!nearly_equal(scale_factor_, previous_scale_factor)) {
        owner_.dispatch_window_event(
            {.type = WindowEvent::Type::ScaleFactorChanged, .scale_factor = scale_factor_});
    }

    if (!nearly_equal(logical_size_.width, previous_logical_size.width) ||
        !nearly_equal(logical_size_.height, previous_logical_size.height)) {
        owner_.dispatch_window_event(
            {.type = WindowEvent::Type::Resize,
             .width = static_cast<int>(std::lround(logical_size_.width)),
             .height = static_cast<int>(std::lround(logical_size_.height))});
    }
}

void Win32Surface::blit_back_buffer(HDC dc) {
    if (dc == nullptr || back_buffer_.empty() || buffer_width_ <= 0 || buffer_height_ <= 0) {
        return;
    }
    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = buffer_width_;
    bitmap_info.bmiHeader.biHeight = -buffer_height_;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;
    StretchDIBits(dc,
                  0,
                  0,
                  static_cast<int>(physical_size_.width),
                  static_cast<int>(physical_size_.height),
                  0,
                  0,
                  buffer_width_,
                  buffer_height_,
                  back_buffer_.data(),
                  &bitmap_info,
                  DIB_RGB_COLORS,
                  SRCCOPY);
}

void Win32Surface::dispatch_mouse_event(MouseEvent event) {
    event.modifiers = query_modifiers();
    owner_.dispatch_mouse_event(event);
}

void Win32Surface::dispatch_key_event(KeyEvent event) {
    event.modifiers = query_modifiers();
    owner_.dispatch_key_event(event);
}

void Win32Surface::dispatch_text_utf16(wchar_t code_unit) {
    if (code_unit < 0x20 && code_unit != L'\r' && code_unit != L'\t') {
        return;
    }
    std::wstring text;
    if (pending_high_surrogate_ != 0) {
        if (code_unit >= 0xDC00 && code_unit <= 0xDFFF) {
            text.push_back(pending_high_surrogate_);
            text.push_back(code_unit);
        }
        pending_high_surrogate_ = 0;
    } else if (code_unit >= 0xD800 && code_unit <= 0xDBFF) {
        pending_high_surrogate_ = code_unit;
        return;
    } else {
        text.push_back(code_unit);
    }

    const auto utf8 = wide_to_utf8(text);
    if (!utf8.empty()) {
        owner_.dispatch_text_input_event({.type = TextInputEvent::Type::Commit, .text = utf8});
    }
}

/// Read one string component (composition or result) from an IME context.
[[nodiscard]] std::wstring ime_composition_string(HIMC context, DWORD index) {
    const LONG bytes = ImmGetCompositionStringW(context, index, nullptr, 0);
    if (bytes <= 0 || bytes % static_cast<LONG>(sizeof(wchar_t)) != 0) {
        return {};
    }
    std::wstring buffer(static_cast<std::size_t>(bytes) / sizeof(wchar_t), L'\0');
    if (ImmGetCompositionStringW(context, index, buffer.data(), bytes) != bytes) {
        return {};
    }
    return buffer;
}

void Win32Surface::handle_ime_composition(LPARAM lparam) {
    HIMC context = ImmGetContext(hwnd_);
    if (context == nullptr) {
        return;
    }
    if ((lparam & GCS_COMPSTR) != 0) {
        // In-progress composition text becomes a Preedit event (or a clear when
        // the composition emptied); the toolkit renders it inline.
        const auto composition = ime_composition_string(context, GCS_COMPSTR);
        const LONG cursor = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
        owner_.dispatch_text_input_event(
            detail::win32_preedit_event_from_composition(composition, static_cast<int>(cursor)));
    }
    if ((lparam & GCS_RESULTSTR) != 0) {
        // Finalized text commits once, here; WM_IME_CHAR is swallowed so the
        // per-character translation does not commit it a second time.
        const auto result = ime_composition_string(context, GCS_RESULTSTR);
        const auto utf8 = wide_to_utf8(result);
        if (!utf8.empty()) {
            owner_.dispatch_text_input_event({.type = TextInputEvent::Type::Commit, .text = utf8});
        }
    }
    ImmReleaseContext(hwnd_, context);
}

LRESULT CALLBACK Win32Surface::window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* surface = reinterpret_cast<Win32Surface*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
        surface = static_cast<Win32Surface*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(surface));
        if (surface != nullptr) {
            surface->hwnd_ = hwnd;
        }
    }
    if (surface == nullptr) {
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    return surface->handle_message(message, wparam, lparam);
}

LRESULT Win32Surface::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_COMMAND: {
        // Menu-origin commands have HIWORD(wparam)==0; HACCEL-origin commands
        // have HIWORD==1. Both carry the command id in LOWORD(wparam) with
        // lparam==0 and dispatch identically.
        const UINT command_id = LOWORD(wparam);
        const bool native_command = lparam == 0 && HIWORD(wparam) <= 1;
        if (native_command && menu_handler_ != nullptr && *menu_handler_) {
            const auto found = menu_command_actions_.find(command_id);
            if (found != menu_command_actions_.end()) {
                (*menu_handler_)(found->second);
                return 0;
            }
        }
        if (native_command) {
            const auto role = menu_command_roles_.find(command_id);
            if (role != menu_command_roles_.end()) {
                dispatch_native_menu_role(role->second);
            }
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd_, &paint);
        blit_back_buffer(dc);
        EndPaint(hwnd_, &paint);
        owner_.dispatch_window_event({.type = WindowEvent::Type::Expose});
        return 0;
    }
    case WM_CLOSE:
        owner_.dispatch_window_event({.type = WindowEvent::Type::Close});
        return 0;
    case WM_GETOBJECT:
        // UI Automation asks with the UiaRootObjectId; everything else (MSAA
        // object ids) falls through to DefWindowProc untouched.
        if (detail::Win32UiaBridge::is_uia_request(lparam)) {
            if (uia_ == nullptr) {
                uia_ = std::make_unique<detail::Win32UiaBridge>(hwnd_, owner_);
            }
            const LRESULT uia_result = uia_->handle_get_object(wparam, lparam);
            if (uia_result != 0) {
                return uia_result;
            }
        }
        break;
    case WM_DESTROY:
        // Purge UIA's event map for this window before the providers go away.
        if (uia_ != nullptr) {
            uia_->disconnect();
        }
        break;
    case WM_SETFOCUS:
        owner_.dispatch_window_event({.type = WindowEvent::Type::FocusIn});
        if (uia_ != nullptr) {
            uia_->synchronize();
        }
        return 0;
    case WM_KILLFOCUS:
        owner_.dispatch_window_event({.type = WindowEvent::Type::FocusOut});
        if (uia_ != nullptr) {
            uia_->synchronize();
        }
        return 0;
    case WM_SIZE:
        if (handling_dpi_change_) {
            return 0;
        }
        {
            const auto previous_logical_size = logical_size_;
            const float previous_scale_factor = scale_factor_;
            update_metrics();
            dispatch_metric_events(previous_logical_size, previous_scale_factor);
        }
        return 0;
    case WM_DPICHANGED: {
        const auto previous_logical_size = logical_size_;
        const float previous_scale_factor = scale_factor_;
        auto* suggested_rect = reinterpret_cast<const RECT*>(lparam);
        handling_dpi_change_ = true;
        if (suggested_rect != nullptr) {
            SetWindowPos(hwnd_,
                         nullptr,
                         suggested_rect->left,
                         suggested_rect->top,
                         suggested_rect->right - suggested_rect->left,
                         suggested_rect->bottom - suggested_rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        handling_dpi_change_ = false;
        update_metrics(LOWORD(wparam));
        dispatch_metric_events(previous_logical_size, previous_scale_factor);
        return 0;
    }
    case WM_SETTINGCHANGE:
    case kWmThemeChange:
        // System visual settings (color scheme, contrast, animation, ...)
        // changed. Re-apply native chrome immediately; the backend's hidden
        // preferences window separately notifies Application with the
        // refreshed preference snapshot.
        apply_color_scheme_chrome(hwnd_, query_color_scheme());
        break;
    case WM_MOUSEMOVE: {
        if (!tracking_mouse_) {
            TRACKMOUSEEVENT track_mouse{};
            track_mouse.cbSize = sizeof(track_mouse);
            track_mouse.dwFlags = TME_LEAVE;
            track_mouse.hwndTrack = hwnd_;
            TrackMouseEvent(&track_mouse);
            tracking_mouse_ = true;
            dispatch_mouse_event({.type = MouseEvent::Type::Enter,
                                  .x = static_cast<float>(GET_X_LPARAM(lparam)) / scale_factor_,
                                  .y = static_cast<float>(GET_Y_LPARAM(lparam)) / scale_factor_});
        }
        dispatch_mouse_event({.type = MouseEvent::Type::Move,
                              .x = static_cast<float>(GET_X_LPARAM(lparam)) / scale_factor_,
                              .y = static_cast<float>(GET_Y_LPARAM(lparam)) / scale_factor_});
        return 0;
    }
    case WM_MOUSELEAVE:
        tracking_mouse_ = false;
        dispatch_mouse_event({.type = MouseEvent::Type::Leave});
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        SetCapture(hwnd_);
        const int button = message == WM_LBUTTONDOWN ? 1 : (message == WM_RBUTTONDOWN ? 2 : 3);
        const int x = GET_X_LPARAM(lparam);
        const int y = GET_Y_LPARAM(lparam);
        press_click_count_ = click_counter_.press(button,
                                                  x,
                                                  y,
                                                  GetTickCount64(),
                                                  GetDoubleClickTime(),
                                                  GetSystemMetrics(SM_CXDOUBLECLK),
                                                  GetSystemMetrics(SM_CYDOUBLECLK));
        dispatch_mouse_event({.type = MouseEvent::Type::Press,
                              .x = static_cast<float>(x) / scale_factor_,
                              .y = static_cast<float>(y) / scale_factor_,
                              .button = button,
                              .click_count = press_click_count_});
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        ReleaseCapture();
        dispatch_mouse_event(
            {.type = MouseEvent::Type::Release,
             .x = static_cast<float>(GET_X_LPARAM(lparam)) / scale_factor_,
             .y = static_cast<float>(GET_Y_LPARAM(lparam)) / scale_factor_,
             .button = message == WM_LBUTTONUP ? 1 : (message == WM_RBUTTONUP ? 2 : 3),
             .click_count = press_click_count_});
        if (uia_ != nullptr) {
            uia_->synchronize();
        }
        return 0;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(hwnd_, &point);
        const float delta =
            static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA);
        dispatch_mouse_event({.type = MouseEvent::Type::Scroll,
                              .x = static_cast<float>(point.x) / scale_factor_,
                              .y = static_cast<float>(point.y) / scale_factor_,
                              .scroll_dx = message == WM_MOUSEHWHEEL ? delta : 0.0F,
                              .scroll_dy = message == WM_MOUSEWHEEL ? delta : 0.0F});
        return 0;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        dispatch_key_event({.type = KeyEvent::Type::Press,
                            .key = translate_key_code(wparam, lparam),
                            .is_repeat = (lparam & 0x40000000) != 0});
        if (uia_ != nullptr) {
            uia_->synchronize();
        }
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        dispatch_key_event(
            {.type = KeyEvent::Type::Release, .key = translate_key_code(wparam, lparam)});
        return 0;
    case WM_IME_SETCONTEXT:
        // Keep the system candidate window but hide the default composition
        // window: the toolkit renders preedit text inline from Preedit events.
        return DefWindowProcW(hwnd_, message, wparam, lparam & ~ISC_SHOWUICOMPOSITIONWINDOW);
    case WM_IME_STARTCOMPOSITION:
        ime_composing_ = true;
        return DefWindowProcW(hwnd_, message, wparam, lparam);
    case WM_IME_COMPOSITION:
        handle_ime_composition(lparam);
        return 0;
    case WM_IME_ENDCOMPOSITION:
        if (ime_composing_) {
            ime_composing_ = false;
            owner_.dispatch_text_input_event(
                {.type = TextInputEvent::Type::ClearPreedit, .text = {}});
        }
        return DefWindowProcW(hwnd_, message, wparam, lparam);
    case WM_IME_CHAR:
        // Composition results are committed through WM_IME_COMPOSITION
        // (GCS_RESULTSTR); swallow the per-character translation so committed
        // text is not delivered twice.
        return 0;
    case WM_CHAR:
        dispatch_text_utf16(static_cast<wchar_t>(wparam));
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            SetCursor(cursor_for_shape(current_cursor_));
            return TRUE;
        }
        break;
    default:
        break;
    }

    return DefWindowProcW(hwnd_, message, wparam, lparam);
}

} // namespace

std::optional<std::string> detail::read_global_unicode_text(HGLOBAL data) {
    constexpr SIZE_T max_bytes = 16U * 1024U * 1024U;
    const auto bytes = GlobalSize(data);
    if (bytes < sizeof(wchar_t) || bytes > max_bytes) {
        return std::nullopt;
    }
    const auto* wide = static_cast<const wchar_t*>(GlobalLock(data));
    if (wide == nullptr) {
        return std::nullopt;
    }

    struct Unlock {
        HGLOBAL handle;

        ~Unlock() { GlobalUnlock(handle); }
    } unlock{data};

    const std::wstring_view buffer(wide, bytes / sizeof(wchar_t));
    const auto end = buffer.find(L'\0');
    if (end == std::wstring_view::npos) {
        return std::nullopt;
    }
    return wide_to_utf8(buffer.substr(0, end));
}

int detail::Win32ClickCounter::press(int button,
                                     int x,
                                     int y,
                                     std::uint64_t now_ms,
                                     std::uint64_t double_click_ms,
                                     int double_click_width,
                                     int double_click_height) {
    // The system double-click rectangle is centered on the previous click.
    const int half_width = std::max(double_click_width / 2, 1);
    const int half_height = std::max(double_click_height / 2, 1);
    const bool same_button = button == last_button_;
    const bool in_time =
        count_ > 0 && now_ms >= last_tick_ && now_ms - last_tick_ <= double_click_ms;
    const bool in_rect =
        std::abs(x - last_x_) <= half_width && std::abs(y - last_y_) <= half_height;
    if (same_button && in_time && in_rect && count_ < std::numeric_limits<int>::max()) {
        ++count_;
    } else {
        count_ = 1;
    }
    last_button_ = button;
    last_x_ = x;
    last_y_ = y;
    last_tick_ = now_ms;
    return count_;
}

UINT detail::win32_virtual_key_for_key(KeyCode key) {
    const auto value = static_cast<int>(key);
    if (value >= static_cast<int>(KeyCode::A) && value <= static_cast<int>(KeyCode::Z)) {
        return static_cast<UINT>('A' + (value - static_cast<int>(KeyCode::A)));
    }
    // KeyCode::Num1..Num9 are contiguous but Num0 sits after Num9.
    if (value >= static_cast<int>(KeyCode::Num1) && value <= static_cast<int>(KeyCode::Num9)) {
        return static_cast<UINT>('1' + (value - static_cast<int>(KeyCode::Num1)));
    }
    if (key == KeyCode::Num0) {
        return '0';
    }
    if (value >= static_cast<int>(KeyCode::F1) && value <= static_cast<int>(KeyCode::F12)) {
        return static_cast<UINT>(VK_F1 + (value - static_cast<int>(KeyCode::F1)));
    }
    // KeyCode::Numpad1..Numpad9 are contiguous but Numpad0 sits after Numpad9.
    if (value >= static_cast<int>(KeyCode::Numpad1) &&
        value <= static_cast<int>(KeyCode::Numpad9)) {
        return static_cast<UINT>(VK_NUMPAD1 + (value - static_cast<int>(KeyCode::Numpad1)));
    }
    if (key == KeyCode::Numpad0) {
        return VK_NUMPAD0;
    }
    switch (key) {
    case KeyCode::Return:
    case KeyCode::NumpadEnter:
        return VK_RETURN;
    case KeyCode::Escape:
        return VK_ESCAPE;
    case KeyCode::Backspace:
        return VK_BACK;
    case KeyCode::Tab:
        return VK_TAB;
    case KeyCode::Space:
        return VK_SPACE;
    case KeyCode::Minus:
        return VK_OEM_MINUS;
    case KeyCode::Equals:
        return VK_OEM_PLUS;
    case KeyCode::LeftBracket:
        return VK_OEM_4;
    case KeyCode::RightBracket:
        return VK_OEM_6;
    case KeyCode::Backslash:
        return VK_OEM_5;
    case KeyCode::Semicolon:
        return VK_OEM_1;
    case KeyCode::Apostrophe:
        return VK_OEM_7;
    case KeyCode::Grave:
        return VK_OEM_3;
    case KeyCode::Comma:
        return VK_OEM_COMMA;
    case KeyCode::Period:
        return VK_OEM_PERIOD;
    case KeyCode::Slash:
        return VK_OEM_2;
    case KeyCode::Insert:
        return VK_INSERT;
    case KeyCode::Home:
        return VK_HOME;
    case KeyCode::PageUp:
        return VK_PRIOR;
    case KeyCode::Delete:
        return VK_DELETE;
    case KeyCode::End:
        return VK_END;
    case KeyCode::PageDown:
        return VK_NEXT;
    case KeyCode::Right:
        return VK_RIGHT;
    case KeyCode::Left:
        return VK_LEFT;
    case KeyCode::Down:
        return VK_DOWN;
    case KeyCode::Up:
        return VK_UP;
    case KeyCode::NumpadDivide:
        return VK_DIVIDE;
    case KeyCode::NumpadMultiply:
        return VK_MULTIPLY;
    case KeyCode::NumpadMinus:
        return VK_SUBTRACT;
    case KeyCode::NumpadPlus:
        return VK_ADD;
    case KeyCode::NumpadPeriod:
        return VK_DECIMAL;
    default:
        return 0;
    }
}

bool detail::win32_accel_for_shortcut(const NativeMenuShortcut& shortcut,
                                      UINT command,
                                      ACCEL* out) {
    if (out == nullptr) {
        return false;
    }
    // Super has no conventional Windows accelerator spelling, and a bare key
    // would swallow ordinary typing; require a real menu modifier combination.
    const auto modifiers = shortcut.modifiers;
    const bool has_ctrl = (modifiers & NativeMenuModifier::Ctrl) != NativeMenuModifier::None;
    const bool has_alt = (modifiers & NativeMenuModifier::Alt) != NativeMenuModifier::None;
    if (!has_ctrl && !has_alt) {
        return false;
    }
    if ((modifiers & NativeMenuModifier::Super) != NativeMenuModifier::None) {
        return false;
    }
    const UINT virtual_key = win32_virtual_key_for_key(shortcut.key);
    if (virtual_key == 0) {
        return false;
    }
    BYTE virt = FVIRTKEY;
    if (has_ctrl) {
        virt |= FCONTROL;
    }
    if (has_alt) {
        virt |= FALT;
    }
    if ((modifiers & NativeMenuModifier::Shift) != NativeMenuModifier::None) {
        virt |= FSHIFT;
    }
    *out = ACCEL{
        .fVirt = virt,
        .key = static_cast<WORD>(virtual_key),
        .cmd = static_cast<WORD>(command),
    };
    return true;
}

TextInputEvent detail::win32_preedit_event_from_composition(std::wstring_view composition,
                                                            int cursor_chars) {
    TextInputEvent event{};
    if (composition.empty()) {
        event.type = TextInputEvent::Type::ClearPreedit;
        return event;
    }
    event.type = TextInputEvent::Type::Preedit;
    event.text = wide_to_utf8(composition);
    // GCS_CURSORPOS is a wchar offset into the composition (negative when the
    // IME reports none); convert it to a UTF-8 byte offset so consumers get
    // the same contract as the Wayland preedit events.
    const std::size_t cursor =
        cursor_chars < 0
            ? composition.size()
            : std::min<std::size_t>(static_cast<std::size_t>(cursor_chars), composition.size());
    event.selection_start = wide_to_utf8(composition.substr(0, cursor)).size();
    event.selection_end = event.selection_start;
    return event;
}

IDataObject* detail::win32_make_drag_data_object(const DragPayload& payload) {
    if (!payload.has_files() && !payload.has_text()) {
        return nullptr;
    }
    return new Win32DragDataObject(payload);
}

float detail::win32_dpi_scale_for_hwnd(HWND hwnd) {
    return query_dpi_scale(hwnd);
}

std::wstring detail::win32_wide_from_utf8(std::string_view text) {
    return utf8_to_wide(text);
}

struct Win32Backend::Impl {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    std::atomic<DWORD> event_loop_thread_id{0};
    EventLoop* current_loop = nullptr;
    std::atomic<int> exit_code{0};
    std::atomic<bool> quit_requested{false};
    bool window_class_registered = false;
    bool preferences_window_class_registered = false;
    bool ole_initialized = false;
    bool app_user_model_id_set = false;
    mutable std::string clipboard_text_cache;
    mutable bool clipboard_text_cache_valid = false;
    mutable bool clipboard_native_write_failed = false;
    Win32SpellChecker spell_checker;
    // App-global native menu model. Win32 menus are per-HWND, so the model is
    // applied to each surface as it is created (see create_surface). Mirrors the
    // app-global NSApp.mainMenu model used by macos_backend.mm.
    std::vector<NativeMenu> app_menu_model;
    NativeMenuActionHandler app_menu_handler;
    // Hidden top-level window receiving WM_SETTINGCHANGE broadcasts; a real
    // (never shown) window because message-only windows do not get broadcasts.
    HWND preferences_window = nullptr;
    PreferencesWatch* preferences_watch = nullptr;
};

Win32Backend::Win32Backend() : impl_(std::make_unique<Impl>()) {}

Win32Backend::~Win32Backend() = default;

Result<void> Win32Backend::initialize() {
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(HANDLE);
    static const auto set_process_dpi_awareness_context =
        reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (set_process_dpi_awareness_context != nullptr) {
        (void)set_process_dpi_awareness_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    } else {
        (void)SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    }

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.lpfnWndProc = &Win32Surface::window_proc;
    window_class.hInstance = impl_->instance;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hbrBackground = nullptr;
    window_class.lpszClassName = kWindowClassName;
    if (RegisterClassExW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return Unexpected(std::string("failed to register Win32 window class"));
    }
    impl_->window_class_registered = true;

    // The hidden preferences-watch window class; registration failure only
    // disables native preference observation (Application falls back to
    // polling), so it is not fatal.
    WNDCLASSEXW preferences_class{};
    preferences_class.cbSize = sizeof(preferences_class);
    preferences_class.lpfnWndProc = &preferences_window_proc;
    preferences_class.hInstance = impl_->instance;
    preferences_class.lpszClassName = kPreferencesWindowClassName;
    if (RegisterClassExW(&preferences_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
        impl_->preferences_window_class_registered = true;
    }

    // OLE is required by the drop-target backend (RegisterDragDrop). OleInitialize
    // layers on top of CoInitializeEx; S_FALSE means the thread was already OLE-
    // initialized, which is fine. We only uninitialize when we own the init.
    const HRESULT ole_hr = OleInitialize(nullptr);
    impl_->ole_initialized = SUCCEEDED(ole_hr);
    if (!impl_->ole_initialized) {
        NK_LOG_WARN("Win32Backend", "OleInitialize failed; drag & drop disabled");
    }
    return {};
}

void Win32Backend::shutdown() {
    stop_system_preferences_observation();
    impl_->app_menu_handler = {};
    impl_->app_menu_model.clear();
    if (impl_->ole_initialized) {
        OleUninitialize();
        impl_->ole_initialized = false;
    }
    if (impl_->preferences_window_class_registered) {
        UnregisterClassW(kPreferencesWindowClassName, impl_->instance);
        impl_->preferences_window_class_registered = false;
    }
    if (impl_->window_class_registered) {
        UnregisterClassW(kWindowClassName, impl_->instance);
        impl_->window_class_registered = false;
    }
}

std::unique_ptr<NativeSurface> Win32Backend::create_surface(const WindowConfig& config,
                                                            Window& owner) {
    if (!config.app_id.empty() && !impl_->app_user_model_id_set) {
        // Taskbar grouping and jump lists key off the explicit AppUserModelID;
        // it must be set before the first window is created to take effect.
        (void)SetCurrentProcessExplicitAppUserModelID(utf8_to_wide(config.app_id).c_str());
        impl_->app_user_model_id_set = true;
    }
    auto surface = std::make_unique<Win32Surface>(impl_->instance, config, owner);
    if (surface->hwnd() == nullptr) {
        NK_LOG_ERROR("Win32Backend", "Failed to create Win32 window surface");
        return nullptr;
    }
    if (!impl_->app_menu_model.empty()) {
        surface->apply_native_menu(impl_->app_menu_model, &impl_->app_menu_handler);
    }
    return surface;
}

int Win32Backend::run_event_loop(EventLoop& loop) {
    impl_->current_loop = &loop;
    impl_->event_loop_thread_id = GetCurrentThreadId();
    loop.set_wake_handler([this] { wake_event_loop(); });
    impl_->quit_requested = false;
    impl_->exit_code = 0;

    MSG message{};
    while (!impl_->quit_requested) {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                impl_->quit_requested = true;
                impl_->exit_code = static_cast<int>(message.wParam);
                break;
            }
            // Translate menu accelerators before normal dispatch: a translated
            // key becomes WM_COMMAND (already dispatched inside the call) and
            // must not also reach the toolkit as a raw key event.
            const auto& accelerators = accelerator_table_registry();
            if (const auto accelerator = accelerators.find(message.hwnd);
                accelerator != accelerators.end() &&
                TranslateAcceleratorW(message.hwnd, accelerator->second, &message) != 0) {
                continue;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (impl_->quit_requested) {
            break;
        }

        loop.poll();
        MsgWaitForMultipleObjectsEx(0, nullptr, 8, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }

    loop.set_wake_handler({});
    impl_->current_loop = nullptr;
    impl_->event_loop_thread_id = 0;
    return impl_->exit_code;
}

void Win32Backend::wake_event_loop() {
    if (impl_->event_loop_thread_id != 0) {
        PostThreadMessageW(impl_->event_loop_thread_id, kWakeMessage, 0, 0);
    }
}

void Win32Backend::request_quit(int exit_code) {
    impl_->exit_code = exit_code;
    impl_->quit_requested = true;
    wake_event_loop();
}

bool Win32Backend::supports_open_file_dialog() const {
    return true;
}

void Win32Backend::show_open_file_dialog_async(std::string_view title,
                                               const std::vector<std::string>& filters,
                                               OpenFileDialogCallback callback) {
    std::string title_str = std::string(title);
    // Capture the active window on the UI thread so the worker-spawned dialog
    // is owned by (and modal to) the application window.
    const HWND owner = GetActiveWindow();
    auto poster = file_dialog_task_poster();
    std::thread([title_str,
                 filters,
                 owner,
                 poster = std::move(poster),
                 callback = std::move(callback)]() mutable {
        auto result = show_modern_open_file_dialog(title_str, filters, owner);
        post_file_dialog_result(poster, std::move(callback), std::move(result));
    }).detach();
}

bool Win32Backend::supports_save_file_dialog() const {
    return true;
}

void Win32Backend::show_save_file_dialog_async(SaveFileDialogOptions options,
                                               SaveFileDialogCallback callback) {
    const HWND owner = GetActiveWindow();
    auto poster = file_dialog_task_poster();
    std::thread([options = std::move(options),
                 owner,
                 poster = std::move(poster),
                 callback = std::move(callback)]() mutable {
        auto result = show_modern_save_file_dialog(options, owner);
        post_file_dialog_result(poster, std::move(callback), std::move(result));
    }).detach();
}

bool Win32Backend::supports_clipboard_text() const {
    return true;
}

std::string Win32Backend::clipboard_text() const {
    if (impl_->clipboard_native_write_failed && impl_->clipboard_text_cache_valid) {
        return impl_->clipboard_text_cache;
    }

    if (!OpenClipboard(nullptr)) {
        return impl_->clipboard_text_cache_valid ? impl_->clipboard_text_cache : std::string{};
    }

    std::string text;
    const HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data != nullptr) {
        text = detail::read_global_unicode_text(data).value_or(std::string{});
    }

    CloseClipboard();
    if (!text.empty()) {
        impl_->clipboard_text_cache = text;
        impl_->clipboard_text_cache_valid = true;
    }
    return text;
}

void Win32Backend::set_clipboard_text(std::string_view text) {
    impl_->clipboard_text_cache = std::string(text);
    impl_->clipboard_text_cache_valid = true;
    impl_->clipboard_native_write_failed = false;

    const auto wide = utf8_to_wide(text);
    if (!OpenClipboard(nullptr)) {
        impl_->clipboard_native_write_failed = true;
        return;
    }

    EmptyClipboard();
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL storage = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (storage != nullptr) {
        auto* destination = static_cast<wchar_t*>(GlobalLock(storage));
        if (destination != nullptr) {
            std::copy(wide.begin(), wide.end(), destination);
            destination[wide.size()] = L'\0';
            GlobalUnlock(storage);
            if (SetClipboardData(CF_UNICODETEXT, storage) != nullptr) {
                storage = nullptr;
            }
        }
    }
    if (storage != nullptr) {
        GlobalFree(storage);
        impl_->clipboard_native_write_failed = true;
    }
    CloseClipboard();
}

SystemPreferences Win32Backend::system_preferences() const {
    return query_system_preferences();
}

bool Win32Backend::supports_system_preferences_observation() const {
    return impl_->preferences_window_class_registered;
}

void Win32Backend::start_system_preferences_observation(SystemPreferencesObserver observer) {
    stop_system_preferences_observation();
    if (!impl_->preferences_window_class_registered || !observer) {
        return;
    }
    // The watch owns the observer and the dedup snapshot; the window procedure
    // reaches it through GWLP_USERDATA, so the backend itself stays opaque.
    auto* watch = new PreferencesWatch{};
    watch->observer = std::move(observer);
    watch->last_observed = query_system_preferences();
    impl_->preferences_window =
        CreateWindowExW(0,
                        kPreferencesWindowClassName,
                        L"",
                        WS_OVERLAPPED, // hidden: never shown, but a real window so
                                       // it receives WM_SETTINGCHANGE broadcasts
                        0,
                        0,
                        0,
                        0,
                        nullptr,
                        nullptr,
                        impl_->instance,
                        watch);
    if (impl_->preferences_window == nullptr) {
        delete watch;
        return;
    }
    impl_->preferences_watch = watch;
}

void Win32Backend::stop_system_preferences_observation() {
    if (impl_->preferences_window != nullptr) {
        auto* watch = impl_->preferences_watch;
        impl_->preferences_watch = nullptr;
        DestroyWindow(impl_->preferences_window);
        impl_->preferences_window = nullptr;
        delete watch;
    }
}

SpellChecker* Win32Backend::spell_checker() {
    return &impl_->spell_checker;
}

bool Win32Backend::supports_native_app_menu() const {
    return true;
}

void Win32Backend::set_native_app_menu(std::span<const NativeMenu> menus,
                                       NativeMenuActionHandler action_handler) {
    impl_->app_menu_model.assign(menus.begin(), menus.end());
    impl_->app_menu_handler = std::move(action_handler);
}

} // namespace nk
