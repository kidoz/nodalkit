#include "win32_backend.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <nk/foundation/logging.h>
#include <nk/platform/window.h>
#include <nk/runtime/event_loop.h>
#include <string>
#include <vector>

#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellscalingapi.h>

namespace nk {

namespace {

constexpr UINT kWakeMessage = WM_APP + 1;
constexpr const wchar_t* kWindowClassName = L"NodalKitWindow";

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
    const int required =
        WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
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
        return static_cast<KeyCode>(static_cast<int>(KeyCode::A) + static_cast<int>(virtual_key - 'A'));
    }
    if (virtual_key >= '0' && virtual_key <= '9') {
        return static_cast<KeyCode>(static_cast<int>(KeyCode::Num0) + static_cast<int>(virtual_key - '0'));
    }
    if (virtual_key >= VK_F1 && virtual_key <= VK_F12) {
        return static_cast<KeyCode>(static_cast<int>(KeyCode::F1) + static_cast<int>(virtual_key - VK_F1));
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

SystemPreferences query_system_preferences() {
    SystemPreferences preferences;
    preferences.platform_family = PlatformFamily::Windows;
    preferences.desktop_environment = DesktopEnvironment::Other;

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
        preferences.color_scheme = ColorScheme::Dark;
    }

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

    preferences.accent_color = query_accent_color();
    return preferences;
}

std::wstring build_open_file_dialog_filter(const std::vector<std::string>& filters) {
    std::wstring filter;
    if (filters.empty()) {
        filter = L"All Files";
        filter.push_back(L'\0');
        filter += L"*.*";
        filter.push_back(L'\0');
        filter.push_back(L'\0');
        return filter;
    }

    for (const auto& item : filters) {
        auto pattern = utf8_to_wide(item);
        if (pattern.empty()) {
            continue;
        }
        filter += pattern;
        filter.push_back(L'\0');
        filter += pattern;
        filter.push_back(L'\0');
    }
    filter.push_back(L'\0');
    return filter;
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

RECT logical_client_rect_for_window(Size logical_size, DWORD style, DWORD ex_style, float scale_factor) {
    const int pixel_width =
        std::max(1, static_cast<int>(std::lround(logical_size.width * std::max(scale_factor, 1.0F))));
    const int pixel_height =
        std::max(1, static_cast<int>(std::lround(logical_size.height * std::max(scale_factor, 1.0F))));
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
    void present(const uint8_t* rgba, int width, int height, std::span<const Rect> damage_regions) override;
    void set_fullscreen(bool fullscreen) override;
    [[nodiscard]] bool is_fullscreen() const override;
    [[nodiscard]] NativeWindowHandle native_handle() const override;
    [[nodiscard]] NativeWindowHandle native_display_handle() const override;
    void set_cursor_shape(CursorShape shape) override;
    [[nodiscard]] HWND hwnd() const;

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

private:
    void update_metrics(UINT dpi_override = 0);
    void dispatch_metric_events(Size previous_logical_size, float previous_scale_factor);
    void blit_back_buffer(HDC dc);
    void dispatch_mouse_event(MouseEvent event);
    void dispatch_key_event(KeyEvent event);
    void dispatch_text_utf16(wchar_t code_unit);
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
};

Win32Surface::Win32Surface(HINSTANCE instance, const WindowConfig& config, Window& owner)
    : instance_(instance), owner_(owner) {
    style_ = config.decorated ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    if (!config.resizable) {
        style_ &= ~WS_THICKFRAME;
        style_ &= ~WS_MAXIMIZEBOX;
    }
    ex_style_ = WS_EX_APPWINDOW;

    const RECT window_rect = logical_client_rect_for_window(
        {static_cast<float>(std::max(1, config.width)), static_cast<float>(std::max(1, config.height))},
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
    }
}

Win32Surface::~Win32Surface() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
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
    const RECT rect = logical_client_rect_for_window({static_cast<float>(std::max(1, width)),
                                                       static_cast<float>(std::max(1, height))},
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
            const auto index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4;
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

        SetWindowLongPtrW(hwnd_, GWL_STYLE, static_cast<LONG_PTR>(windowed_style_ & ~WS_OVERLAPPEDWINDOW));
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

void Win32Surface::update_metrics(UINT dpi_override) {
    if (hwnd_ == nullptr) {
        return;
    }
    scale_factor_ = dpi_override != 0 ? scale_factor_from_dpi(dpi_override) : query_dpi_scale(hwnd_);
    RECT client_rect{};
    GetClientRect(hwnd_, &client_rect);
    physical_size_ = {static_cast<float>(std::max<LONG>(0, client_rect.right - client_rect.left)),
                      static_cast<float>(std::max<LONG>(0, client_rect.bottom - client_rect.top))};
    logical_size_ = {scale_factor_ > 0.0F ? physical_size_.width / scale_factor_ : physical_size_.width,
                     scale_factor_ > 0.0F ? physical_size_.height / scale_factor_ : physical_size_.height};
}

void Win32Surface::dispatch_metric_events(Size previous_logical_size, float previous_scale_factor) {
    if (!nearly_equal(scale_factor_, previous_scale_factor)) {
        owner_.dispatch_window_event(
            {.type = WindowEvent::Type::ScaleFactorChanged, .scale_factor = scale_factor_});
    }

    if (!nearly_equal(logical_size_.width, previous_logical_size.width) ||
        !nearly_equal(logical_size_.height, previous_logical_size.height)) {
        owner_.dispatch_window_event({.type = WindowEvent::Type::Resize,
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
    case WM_SETFOCUS:
        owner_.dispatch_window_event({.type = WindowEvent::Type::FocusIn});
        return 0;
    case WM_KILLFOCUS:
        owner_.dispatch_window_event({.type = WindowEvent::Type::FocusOut});
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
    case WM_MOUSEMOVE: {
        if (!tracking_mouse_) {
            TRACKMOUSEEVENT track_mouse{};
            track_mouse.cbSize = sizeof(track_mouse);
            track_mouse.dwFlags = TME_LEAVE;
            track_mouse.hwndTrack = hwnd_;
            TrackMouseEvent(&track_mouse);
            tracking_mouse_ = true;
            dispatch_mouse_event(
                {.type = MouseEvent::Type::Enter,
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
    case WM_MBUTTONDOWN:
        SetCapture(hwnd_);
        dispatch_mouse_event({.type = MouseEvent::Type::Press,
                              .x = static_cast<float>(GET_X_LPARAM(lparam)) / scale_factor_,
                              .y = static_cast<float>(GET_Y_LPARAM(lparam)) / scale_factor_,
                              .button = message == WM_LBUTTONDOWN ? 1 : (message == WM_RBUTTONDOWN ? 2 : 3)});
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        ReleaseCapture();
        dispatch_mouse_event({.type = MouseEvent::Type::Release,
                              .x = static_cast<float>(GET_X_LPARAM(lparam)) / scale_factor_,
                              .y = static_cast<float>(GET_Y_LPARAM(lparam)) / scale_factor_,
                              .button = message == WM_LBUTTONUP ? 1 : (message == WM_RBUTTONUP ? 2 : 3)});
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
        return 0;
    case WM_KEYUP:
    case WM_SYSKEYUP:
        dispatch_key_event({.type = KeyEvent::Type::Release, .key = translate_key_code(wparam, lparam)});
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

struct Win32Backend::Impl {
    HINSTANCE instance = GetModuleHandleW(nullptr);
    DWORD event_loop_thread_id = 0;
    EventLoop* current_loop = nullptr;
    int exit_code = 0;
    bool quit_requested = false;
    bool window_class_registered = false;
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
    return {};
}

void Win32Backend::shutdown() {
    if (impl_->window_class_registered) {
        UnregisterClassW(kWindowClassName, impl_->instance);
        impl_->window_class_registered = false;
    }
}

std::unique_ptr<NativeSurface> Win32Backend::create_surface(const WindowConfig& config, Window& owner) {
    auto surface = std::make_unique<Win32Surface>(impl_->instance, config, owner);
    if (surface->hwnd() == nullptr) {
        NK_LOG_ERROR("Win32Backend", "Failed to create Win32 window surface");
        return nullptr;
    }
    return surface;
}

int Win32Backend::run_event_loop(EventLoop& loop) {
    impl_->current_loop = &loop;
    impl_->event_loop_thread_id = GetCurrentThreadId();
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
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (impl_->quit_requested) {
            break;
        }

        loop.poll();
        MsgWaitForMultipleObjectsEx(0, nullptr, 8, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }

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

OpenFileDialogResult Win32Backend::show_open_file_dialog(std::string_view title,
                                                         const std::vector<std::string>& filters) {
    std::array<wchar_t, MAX_PATH> file_buffer{};
    auto filter = build_open_file_dialog_filter(filters);
    auto wide_title = utf8_to_wide(title);

    OPENFILENAMEW open_file{};
    open_file.lStructSize = sizeof(open_file);
    open_file.lpstrFile = file_buffer.data();
    open_file.nMaxFile = static_cast<DWORD>(file_buffer.size());
    open_file.lpstrFilter = filter.c_str();
    open_file.nFilterIndex = 1;
    open_file.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    open_file.lpstrTitle = wide_title.empty() ? nullptr : wide_title.c_str();

    if (!GetOpenFileNameW(&open_file)) {
        const DWORD error = CommDlgExtendedError();
        return Unexpected(error == 0 ? FileDialogError::Cancelled : FileDialogError::Failed);
    }
    return wide_to_utf8(file_buffer.data());
}

bool Win32Backend::supports_clipboard_text() const {
    return true;
}

std::string Win32Backend::clipboard_text() const {
    if (!OpenClipboard(nullptr)) {
        return {};
    }

    std::string text;
    const HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (data != nullptr) {
        const auto* wide = static_cast<const wchar_t*>(GlobalLock(data));
        if (wide != nullptr) {
            text = wide_to_utf8(wide);
            GlobalUnlock(data);
        }
    }

    CloseClipboard();
    return text;
}

void Win32Backend::set_clipboard_text(std::string_view text) {
    const auto wide = utf8_to_wide(text);
    if (!OpenClipboard(nullptr)) {
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
            SetClipboardData(CF_UNICODETEXT, storage);
            storage = nullptr;
        }
    }
    if (storage != nullptr) {
        GlobalFree(storage);
    }
    CloseClipboard();
}

SystemPreferences Win32Backend::system_preferences() const {
    return query_system_preferences();
}

} // namespace nk
