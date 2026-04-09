#include "gdi_text_shaper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

namespace nk {

namespace {

std::wstring utf8_to_wide(std::string_view text) {
    if (text.empty()) {
        return {};
    }

    const int required = MultiByteToWideChar(CP_UTF8,
                                             MB_ERR_INVALID_CHARS,
                                             text.data(),
                                             static_cast<int>(text.size()),
                                             nullptr,
                                             0);
    if (required <= 0) {
        return {};
    }

    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(CP_UTF8,
                                              MB_ERR_INVALID_CHARS,
                                              text.data(),
                                              static_cast<int>(text.size()),
                                              wide.data(),
                                              required);
    if (converted != required) {
        return {};
    }
    return wide;
}

int font_weight_to_gdi(FontWeight weight) {
    return static_cast<int>(weight);
}

LONG font_height_pixels(float point_size) {
    HDC screen_dc = GetDC(nullptr);
    const int dpi = screen_dc != nullptr ? GetDeviceCaps(screen_dc, LOGPIXELSY) : 96;
    if (screen_dc != nullptr) {
        ReleaseDC(nullptr, screen_dc);
    }
    const auto scaled = std::max(1, static_cast<int>(std::lround(point_size * dpi / 72.0F)));
    return -scaled;
}

std::wstring resolve_font_family(const FontDescriptor& font) {
    if (font.family.empty() || font.family == "System") {
        return L"Segoe UI";
    }
    auto family = utf8_to_wide(font.family);
    return family.empty() ? L"Segoe UI" : family;
}

HFONT create_font(const FontDescriptor& font) {
    const auto family = resolve_font_family(font);
    return CreateFontW(font_height_pixels(font.size),
                       0,
                       0,
                       0,
                       font_weight_to_gdi(font.weight),
                       font.style != FontStyle::Normal,
                       FALSE,
                       FALSE,
                       DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE,
                       family.c_str());
}

struct ScopedFontSelection {
    HDC dc = nullptr;
    HFONT font = nullptr;
    HGDIOBJ previous = nullptr;

    explicit ScopedFontSelection(HDC target_dc, HFONT selected_font) : dc(target_dc), font(selected_font) {
        if (dc != nullptr && font != nullptr) {
            previous = SelectObject(dc, font);
        }
    }

    ~ScopedFontSelection() {
        if (dc != nullptr && previous != nullptr) {
            SelectObject(dc, previous);
        }
        if (font != nullptr) {
            DeleteObject(font);
        }
    }
};

struct TextMetricsData {
    SIZE extent{};
    TEXTMETRICW metrics{};
};

TextMetricsData measure_text(HDC dc, std::wstring_view text) {
    TextMetricsData data;
    if (dc == nullptr) {
        return data;
    }

    GetTextMetricsW(dc, &data.metrics);
    if (!text.empty()) {
        GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &data.extent);
    }
    data.extent.cy = std::max<LONG>(data.extent.cy, data.metrics.tmHeight);
    return data;
}

std::vector<uint8_t> rasterize_text_bitmap(std::wstring_view text,
                                           const FontDescriptor& font,
                                           Color color,
                                           int width,
                                           int height) {
    std::vector<uint8_t> rgba(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                              4,
                              0);
    if (text.empty() || width <= 0 || height <= 0) {
        return rgba;
    }

    BITMAPINFO bitmap_info{};
    bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap_info.bmiHeader.biWidth = width;
    bitmap_info.bmiHeader.biHeight = -height;
    bitmap_info.bmiHeader.biPlanes = 1;
    bitmap_info.bmiHeader.biBitCount = 32;
    bitmap_info.bmiHeader.biCompression = BI_RGB;

    HDC screen_dc = GetDC(nullptr);
    HDC memory_dc = screen_dc != nullptr ? CreateCompatibleDC(screen_dc) : nullptr;
    void* bits = nullptr;
    HBITMAP bitmap =
        memory_dc != nullptr ? CreateDIBSection(memory_dc, &bitmap_info, DIB_RGB_COLORS, &bits, nullptr, 0)
                             : nullptr;

    if (screen_dc == nullptr || memory_dc == nullptr || bitmap == nullptr || bits == nullptr) {
        if (bitmap != nullptr) {
            DeleteObject(bitmap);
        }
        if (memory_dc != nullptr) {
            DeleteDC(memory_dc);
        }
        if (screen_dc != nullptr) {
            ReleaseDC(nullptr, screen_dc);
        }
        return rgba;
    }

    auto* pixels = static_cast<uint32_t*>(bits);
    std::fill(pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0x00FFFFFFU);

    const HGDIOBJ previous_bitmap = SelectObject(memory_dc, bitmap);
    const ScopedFontSelection font_selection(memory_dc, create_font(font));
    SetBkMode(memory_dc, OPAQUE);
    SetBkColor(memory_dc, RGB(255, 255, 255));
    SetTextColor(memory_dc, RGB(0, 0, 0));
    TextOutW(memory_dc, 0, 0, text.data(), static_cast<int>(text.size()));

    const auto red = static_cast<uint8_t>(std::clamp(color.r * 255.0F, 0.0F, 255.0F));
    const auto green = static_cast<uint8_t>(std::clamp(color.g * 255.0F, 0.0F, 255.0F));
    const auto blue = static_cast<uint8_t>(std::clamp(color.b * 255.0F, 0.0F, 255.0F));
    const auto alpha_scale = std::clamp(color.a, 0.0F, 1.0F);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const auto pixel = pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(x)];
            const uint8_t pixel_red = static_cast<uint8_t>((pixel >> 16) & 0xFF);
            const uint8_t coverage = static_cast<uint8_t>(
                std::clamp((255.0F - static_cast<float>(pixel_red)) * alpha_scale, 0.0F, 255.0F));
            const auto index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) *
                4;
            rgba[index + 0] = red;
            rgba[index + 1] = green;
            rgba[index + 2] = blue;
            rgba[index + 3] = coverage;
        }
    }

    if (previous_bitmap != nullptr) {
        SelectObject(memory_dc, previous_bitmap);
    }
    DeleteObject(bitmap);
    DeleteDC(memory_dc);
    ReleaseDC(nullptr, screen_dc);
    return rgba;
}

struct TrimmedBitmap {
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    int top_crop = 0;
};

TrimmedBitmap trim_bitmap_rows(std::vector<uint8_t> rgba, int width, int height) {
    TrimmedBitmap trimmed{std::move(rgba), width, height, 0};
    if (width <= 0 || height <= 0 || trimmed.pixels.empty()) {
        return trimmed;
    }

    int first_row = -1;
    int last_row = -1;
    for (int y = 0; y < height; ++y) {
        bool has_alpha = false;
        for (int x = 0; x < width; ++x) {
            const auto index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) *
                    4 +
                3;
            if (trimmed.pixels[index] != 0) {
                has_alpha = true;
                break;
            }
        }
        if (has_alpha) {
            if (first_row < 0) {
                first_row = y;
            }
            last_row = y;
        }
    }

    if (first_row <= 0 || last_row < first_row || last_row == height - 1) {
        return trimmed;
    }

    const int cropped_height = last_row - first_row + 1;
    std::vector<uint8_t> cropped(static_cast<std::size_t>(width) * static_cast<std::size_t>(cropped_height) * 4,
                                 0);
    for (int y = 0; y < cropped_height; ++y) {
        const auto* source = trimmed.pixels.data() +
                             static_cast<std::size_t>(first_row + y) * static_cast<std::size_t>(width) * 4;
        auto* destination =
            cropped.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4;
        std::copy(source, source + static_cast<std::size_t>(width) * 4, destination);
    }

    trimmed.pixels = std::move(cropped);
    trimmed.height = cropped_height;
    trimmed.top_crop = first_row;
    return trimmed;
}

} // namespace

GdiTextShaper::GdiTextShaper() = default;
GdiTextShaper::~GdiTextShaper() = default;

Size GdiTextShaper::measure(std::string_view text, const FontDescriptor& font) const {
    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        return {};
    }

    const auto wide = utf8_to_wide(text);
    const ScopedFontSelection font_selection(screen_dc, create_font(font));
    const auto metrics = measure_text(screen_dc, wide);
    ReleaseDC(nullptr, screen_dc);

    return {
        static_cast<float>(metrics.extent.cx),
        static_cast<float>(metrics.extent.cy),
    };
}

ShapedText GdiTextShaper::shape(std::string_view text, const FontDescriptor& font, Color color) const {
    ShapedText shaped;
    const auto wide = utf8_to_wide(text);

    HDC screen_dc = GetDC(nullptr);
    if (screen_dc == nullptr) {
        return shaped;
    }

    const ScopedFontSelection font_selection(screen_dc, create_font(font));
    const auto metrics = measure_text(screen_dc, wide);
    ReleaseDC(nullptr, screen_dc);

    const int bitmap_width = std::max<LONG>(1, metrics.extent.cx);
    const int bitmap_height = std::max<LONG>(1, metrics.extent.cy);
    auto trimmed =
        trim_bitmap_rows(rasterize_text_bitmap(wide, font, color, bitmap_width, bitmap_height),
                         bitmap_width,
                         bitmap_height);
    shaped.set_text_size({
        static_cast<float>(metrics.extent.cx),
        static_cast<float>(trimmed.height),
    });
    shaped.set_baseline(
        static_cast<float>(std::max<LONG>(0, metrics.metrics.tmAscent - trimmed.top_crop)));
    shaped.set_bitmap(std::move(trimmed.pixels), trimmed.width, trimmed.height);
    return shaped;
}

} // namespace nk
