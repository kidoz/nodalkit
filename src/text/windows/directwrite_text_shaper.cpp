#include "directwrite_text_shaper.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <dwrite.h>
#include <string>
#include <vector>
#include <windows.h>

namespace nk {

namespace {

template <typename T> void safe_release(T*& ptr) {
    if (ptr != nullptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

template <typename Interface> REFIID interface_iid() {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif
    return __uuidof(Interface);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

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

std::wstring resolve_font_family(const FontDescriptor& font) {
    if (font.family.empty() || font.family == "System") {
        return L"Segoe UI";
    }

    auto family = utf8_to_wide(font.family);
    return family.empty() ? L"Segoe UI" : family;
}

float font_em_size_dips(float logical_pixel_size) {
    return std::max(1.0F, logical_pixel_size);
}

float system_pixels_per_dip() {
    HDC screen_dc = GetDC(nullptr);
    const int dpi_x = screen_dc != nullptr ? GetDeviceCaps(screen_dc, LOGPIXELSX) : 96;
    if (screen_dc != nullptr) {
        ReleaseDC(nullptr, screen_dc);
    }
    return std::max(1.0F, static_cast<float>(dpi_x) / 96.0F);
}

struct TextLayoutMetrics {
    float width = 0.0F;
    float height = 0.0F;
    float baseline = 0.0F;
};

class BitmapTextRenderer final : public IDWriteTextRenderer {
public:
    BitmapTextRenderer(IDWriteBitmapRenderTarget* target,
                       IDWriteRenderingParams* rendering_params,
                       COLORREF text_color,
                       float pixels_per_dip)
        : target_(target)
        , rendering_params_(rendering_params)
        , text_color_(text_color)
        , pixels_per_dip_(pixels_per_dip) {
        if (target_ != nullptr) {
            target_->AddRef();
        }
        if (rendering_params_ != nullptr) {
            rendering_params_->AddRef();
        }
    }

    ~BitmapTextRenderer() {
        safe_release(rendering_params_);
        safe_release(target_);
    }

    IFACEMETHODIMP QueryInterface(REFIID iid, void** object) noexcept {
        if (object == nullptr) {
            return E_POINTER;
        }

        *object = nullptr;
        if (iid == interface_iid<IUnknown>() || iid == interface_iid<IDWritePixelSnapping>() ||
            iid == interface_iid<IDWriteTextRenderer>()) {
            *object = static_cast<IDWriteTextRenderer*>(this);
            AddRef();
            return S_OK;
        }

        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() noexcept {
        return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
    }

    IFACEMETHODIMP_(ULONG) Release() noexcept {
        const auto count = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
        if (count == 0) {
            delete this;
        }
        return count;
    }

    IFACEMETHODIMP IsPixelSnappingDisabled(void*, BOOL* is_disabled) noexcept {
        if (is_disabled == nullptr) {
            return E_POINTER;
        }
        *is_disabled = FALSE;
        return S_OK;
    }

    IFACEMETHODIMP GetCurrentTransform(void*, DWRITE_MATRIX* transform) noexcept {
        if (transform == nullptr) {
            return E_POINTER;
        }
        *transform = DWRITE_MATRIX{1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
        return S_OK;
    }

    IFACEMETHODIMP GetPixelsPerDip(void*, FLOAT* pixels_per_dip) noexcept {
        if (pixels_per_dip == nullptr) {
            return E_POINTER;
        }
        *pixels_per_dip = pixels_per_dip_;
        return S_OK;
    }

    IFACEMETHODIMP DrawGlyphRun(void*,
                                FLOAT baseline_origin_x,
                                FLOAT baseline_origin_y,
                                DWRITE_MEASURING_MODE measuring_mode,
                                DWRITE_GLYPH_RUN const* glyph_run,
                                DWRITE_GLYPH_RUN_DESCRIPTION const*,
                                IUnknown*) noexcept {
        if (target_ == nullptr || rendering_params_ == nullptr || glyph_run == nullptr) {
            return E_INVALIDARG;
        }

        RECT black_box{};
        return target_->DrawGlyphRun(baseline_origin_x,
                                     baseline_origin_y,
                                     measuring_mode,
                                     glyph_run,
                                     rendering_params_,
                                     text_color_,
                                     &black_box);
    }

    IFACEMETHODIMP DrawUnderline(void*, FLOAT, FLOAT, DWRITE_UNDERLINE const*, IUnknown*) noexcept {
        return S_OK;
    }

    IFACEMETHODIMP
    DrawStrikethrough(void*, FLOAT, FLOAT, DWRITE_STRIKETHROUGH const*, IUnknown*) noexcept {
        return S_OK;
    }

    IFACEMETHODIMP
    DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) noexcept {
        return S_OK;
    }

private:
    volatile LONG ref_count_ = 1;
    IDWriteBitmapRenderTarget* target_ = nullptr;
    IDWriteRenderingParams* rendering_params_ = nullptr;
    COLORREF text_color_ = RGB(0, 0, 0);
    float pixels_per_dip_ = 1.0F;
};

} // namespace

struct DirectWriteTextShaper::Impl {
    IDWriteFactory* factory = nullptr;
    IDWriteGdiInterop* gdi_interop = nullptr;
    IDWriteRenderingParams* grayscale_rendering_params = nullptr;
    float pixels_per_dip = 1.0F;

    ~Impl() {
        safe_release(grayscale_rendering_params);
        safe_release(gdi_interop);
        safe_release(factory);
    }

    [[nodiscard]] bool available() const {
        return factory != nullptr && gdi_interop != nullptr &&
               grayscale_rendering_params != nullptr;
    }

    bool initialize() {
        IUnknown* created_factory = nullptr;
        if (FAILED(DWriteCreateFactory(
                DWRITE_FACTORY_TYPE_SHARED, interface_iid<IDWriteFactory>(), &created_factory))) {
            return false;
        }
        factory = static_cast<IDWriteFactory*>(created_factory);

        if (FAILED(factory->GetGdiInterop(&gdi_interop))) {
            return false;
        }

        pixels_per_dip = system_pixels_per_dip();
        if (FAILED(factory->CreateCustomRenderingParams(2.2F,
                                                        0.0F,
                                                        0.0F,
                                                        DWRITE_PIXEL_GEOMETRY_FLAT,
                                                        DWRITE_RENDERING_MODE_NATURAL,
                                                        &grayscale_rendering_params))) {
            return false;
        }

        return true;
    }

    [[nodiscard]] IDWriteTextFormat* create_text_format(const FontDescriptor& font) const {
        if (factory == nullptr) {
            return nullptr;
        }

        IDWriteTextFormat* format = nullptr;
        const auto family = resolve_font_family(font);

        DWRITE_FONT_STYLE style = DWRITE_FONT_STYLE_NORMAL;
        if (font.style == FontStyle::Italic) {
            style = DWRITE_FONT_STYLE_ITALIC;
        } else if (font.style == FontStyle::Oblique) {
            style = DWRITE_FONT_STYLE_OBLIQUE;
        }

        if (FAILED(factory->CreateTextFormat(family.c_str(),
                                             nullptr,
                                             static_cast<DWRITE_FONT_WEIGHT>(font.weight),
                                             style,
                                             DWRITE_FONT_STRETCH_NORMAL,
                                             font_em_size_dips(font.size),
                                             L"en-us",
                                             &format))) {
            return nullptr;
        }

        format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        return format;
    }

    [[nodiscard]] IDWriteTextLayout* create_text_layout(std::wstring_view text,
                                                        IDWriteTextFormat* format) const {
        if (factory == nullptr || format == nullptr) {
            return nullptr;
        }

        IDWriteTextLayout* layout = nullptr;
        constexpr FLOAT kMaxLayoutWidth = 16384.0F;
        constexpr FLOAT kMaxLayoutHeight = 4096.0F;
        if (FAILED(factory->CreateTextLayout(text.data(),
                                             static_cast<UINT32>(text.size()),
                                             format,
                                             kMaxLayoutWidth,
                                             kMaxLayoutHeight,
                                             &layout))) {
            return nullptr;
        }

        return layout;
    }

    [[nodiscard]] TextLayoutMetrics layout_metrics(IDWriteTextLayout* layout) const {
        TextLayoutMetrics result;
        if (layout == nullptr) {
            return result;
        }

        DWRITE_TEXT_METRICS metrics{};
        if (FAILED(layout->GetMetrics(&metrics))) {
            return result;
        }

        UINT32 line_count = 0;
        layout->GetLineMetrics(nullptr, 0, &line_count);
        if (line_count > 0) {
            std::vector<DWRITE_LINE_METRICS> lines(static_cast<std::size_t>(line_count));
            if (SUCCEEDED(layout->GetLineMetrics(lines.data(), line_count, &line_count)) &&
                !lines.empty()) {
                result.baseline = lines.front().baseline;
            }
        }

        result.width = metrics.widthIncludingTrailingWhitespace;
        result.height = metrics.height;
        return result;
    }

    [[nodiscard]] std::vector<uint8_t> extract_rgba_bitmap(IDWriteBitmapRenderTarget* render_target,
                                                           int width,
                                                           int height,
                                                           Color color) const {
        std::vector<uint8_t> rgba(
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
        if (render_target == nullptr || width <= 0 || height <= 0) {
            return rgba;
        }

        HDC memory_dc = render_target->GetMemoryDC();
        if (memory_dc == nullptr) {
            return rgba;
        }

        const auto render_bitmap = static_cast<HBITMAP>(GetCurrentObject(memory_dc, OBJ_BITMAP));
        if (render_bitmap == nullptr) {
            return rgba;
        }

        DIBSECTION dib{};
        if (GetObjectW(render_bitmap, sizeof(dib), &dib) != sizeof(dib) ||
            dib.dsBm.bmBits == nullptr) {
            return rgba;
        }

        const auto stride = static_cast<std::size_t>(dib.dsBm.bmWidthBytes);
        const auto* bgra = static_cast<const uint8_t*>(dib.dsBm.bmBits);
        const auto red = static_cast<uint8_t>(std::clamp(color.r * 255.0F, 0.0F, 255.0F));
        const auto green = static_cast<uint8_t>(std::clamp(color.g * 255.0F, 0.0F, 255.0F));
        const auto blue = static_cast<uint8_t>(std::clamp(color.b * 255.0F, 0.0F, 255.0F));
        const auto alpha_scale = std::clamp(color.a, 0.0F, 1.0F);

        for (int y = 0; y < height; ++y) {
            const auto* source_row = bgra + static_cast<std::size_t>(y) * stride;
            for (int x = 0; x < width; ++x) {
                const auto source_index = static_cast<std::size_t>(x) * 4;
                const uint8_t source_red = source_row[source_index + 2];
                const uint8_t coverage = static_cast<uint8_t>(std::clamp(
                    (255.0F - static_cast<float>(source_red)) * alpha_scale, 0.0F, 255.0F));

                const auto destination_index =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)) *
                    4;
                rgba[destination_index + 0] = red;
                rgba[destination_index + 1] = green;
                rgba[destination_index + 2] = blue;
                rgba[destination_index + 3] = coverage;
            }
        }

        return rgba;
    }
};

DirectWriteTextShaper::DirectWriteTextShaper() : impl_(std::make_unique<Impl>()) {
    (void)impl_->initialize();
}

DirectWriteTextShaper::~DirectWriteTextShaper() = default;

Size DirectWriteTextShaper::measure(std::string_view text, const FontDescriptor& font) const {
    if (!impl_->available()) {
        return fallback_.measure(text, font);
    }

    const auto wide = utf8_to_wide(text);
    IDWriteTextFormat* format = impl_->create_text_format(font);
    if (format == nullptr) {
        return fallback_.measure(text, font);
    }

    IDWriteTextLayout* layout = impl_->create_text_layout(wide, format);
    format->Release();
    if (layout == nullptr) {
        return fallback_.measure(text, font);
    }

    const auto metrics = impl_->layout_metrics(layout);
    layout->Release();
    return {
        std::ceil(metrics.width),
        std::ceil(metrics.height),
    };
}

ShapedText
DirectWriteTextShaper::shape(std::string_view text, const FontDescriptor& font, Color color) const {
    if (!impl_->available()) {
        return fallback_.shape(text, font, color);
    }

    const auto wide = utf8_to_wide(text);
    IDWriteTextFormat* format = impl_->create_text_format(font);
    if (format == nullptr) {
        return fallback_.shape(text, font, color);
    }

    IDWriteTextLayout* layout = impl_->create_text_layout(wide, format);
    format->Release();
    if (layout == nullptr) {
        return fallback_.shape(text, font, color);
    }

    const auto metrics = impl_->layout_metrics(layout);
    const float ppd = impl_->pixels_per_dip;
    const int bitmap_width = std::max(1, static_cast<int>(std::ceil(metrics.width * ppd)));
    const int bitmap_height = std::max(1, static_cast<int>(std::ceil(metrics.height * ppd)));

    HDC screen_dc = GetDC(nullptr);
    IDWriteBitmapRenderTarget* render_target = nullptr;
    if (screen_dc == nullptr || impl_->gdi_interop == nullptr ||
        FAILED(impl_->gdi_interop->CreateBitmapRenderTarget(
            screen_dc, bitmap_width, bitmap_height, &render_target))) {
        if (screen_dc != nullptr) {
            ReleaseDC(nullptr, screen_dc);
        }
        layout->Release();
        return fallback_.shape(text, font, color);
    }

    (void)render_target->SetPixelsPerDip(impl_->pixels_per_dip);
    DWRITE_MATRIX identity{1.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F};
    (void)render_target->SetCurrentTransform(&identity);

    HDC memory_dc = render_target->GetMemoryDC();
    RECT clear_rect{0, 0, bitmap_width, bitmap_height};
    HBRUSH clear_brush = CreateSolidBrush(RGB(255, 255, 255));
    if (memory_dc != nullptr && clear_brush != nullptr) {
        FillRect(memory_dc, &clear_rect, clear_brush);
    }
    if (clear_brush != nullptr) {
        DeleteObject(clear_brush);
    }

    auto* renderer = new BitmapTextRenderer(
        render_target, impl_->grayscale_rendering_params, RGB(0, 0, 0), impl_->pixels_per_dip);
    const HRESULT draw_result = layout->Draw(nullptr, renderer, 0.0F, 0.0F);
    renderer->Release();

    std::vector<uint8_t> rgba;
    if (SUCCEEDED(draw_result)) {
        rgba = impl_->extract_rgba_bitmap(render_target, bitmap_width, bitmap_height, color);
    }

    render_target->Release();
    ReleaseDC(nullptr, screen_dc);
    layout->Release();

    if (rgba.empty()) {
        return fallback_.shape(text, font, color);
    }

    ShapedText shaped;
    shaped.set_text_size({
        std::ceil(metrics.width),
        std::ceil(metrics.height),
    });
    shaped.set_baseline(std::max(0.0F, metrics.baseline));
    // Preserve the full raster height. The current ShapedText contract does
    // not carry bitmap bearings, so trimming leading transparent rows would
    // shift glyph coverage upward at draw time.
    shaped.set_bitmap(std::move(rgba), bitmap_width, bitmap_height);
    return shaped;
}

} // namespace nk
