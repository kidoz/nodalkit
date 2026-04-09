/// @file d3d11_renderer.cpp
/// @brief Windows D3D11 swap-chain renderer backed by software raster output.

#include <nk/render/renderer.h>

#if defined(_WIN32)

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <nk/foundation/logging.h>
#include <nk/platform/platform_backend.h>
#include <vector>

namespace nk {

namespace {

template <typename T>
void safe_release(T*& ptr) {
    if (ptr != nullptr) {
        ptr->Release();
        ptr = nullptr;
    }
}

template <typename Interface>
REFIID interface_iid() {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif
    return __uuidof(Interface);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

float normalize_scale_factor(float scale_factor) {
    return std::isfinite(scale_factor) && scale_factor > 0.0F ? scale_factor : 1.0F;
}

RECT clamp_rect_to_framebuffer(Rect rect, int width, int height, float scale_factor) {
    const float normalized_scale = normalize_scale_factor(scale_factor);
    const int left = std::clamp(static_cast<int>(std::floor(rect.x * normalized_scale)), 0, width);
    const int top = std::clamp(static_cast<int>(std::floor(rect.y * normalized_scale)), 0, height);
    const int right =
        std::clamp(static_cast<int>(std::ceil(rect.right() * normalized_scale)), 0, width);
    const int bottom =
        std::clamp(static_cast<int>(std::ceil(rect.bottom() * normalized_scale)), 0, height);
    return {
        left,
        top,
        std::max(left, right),
        std::max(top, bottom),
    };
}

bool rect_is_empty(const RECT& rect) {
    return rect.right <= rect.left || rect.bottom <= rect.top;
}

bool rects_overlap_or_touch(const RECT& lhs, const RECT& rhs) {
    return lhs.left <= rhs.right && lhs.right >= rhs.left && lhs.top <= rhs.bottom &&
           lhs.bottom >= rhs.top;
}

RECT union_rects(const RECT& lhs, const RECT& rhs) {
    return {
        std::min(lhs.left, rhs.left),
        std::min(lhs.top, rhs.top),
        std::max(lhs.right, rhs.right),
        std::max(lhs.bottom, rhs.bottom),
    };
}

const char* dxgi_result_name(HRESULT result) {
    switch (result) {
    case S_OK:
        return "S_OK";
    case DXGI_STATUS_OCCLUDED:
        return "DXGI_STATUS_OCCLUDED";
    case DXGI_ERROR_DEVICE_REMOVED:
        return "DXGI_ERROR_DEVICE_REMOVED";
    case DXGI_ERROR_DEVICE_RESET:
        return "DXGI_ERROR_DEVICE_RESET";
    case DXGI_ERROR_DEVICE_HUNG:
        return "DXGI_ERROR_DEVICE_HUNG";
    case DXGI_ERROR_INVALID_CALL:
        return "DXGI_ERROR_INVALID_CALL";
    default:
        return "DXGI_ERROR_UNKNOWN";
    }
}

void log_dxgi_failure(std::string_view step, HRESULT result) {
    std::string message = "D3D11 step failed: ";
    message += step;
    message += " (";
    message += dxgi_result_name(result);
    message += ")";
    NK_LOG_WARN("D3D11Renderer", message);
}

class D3D11Renderer final : public Renderer {
public:
    D3D11Renderer() : software_(std::make_unique<SoftwareRenderer>()) {}

    ~D3D11Renderer() override { destroy_context(); }

    [[nodiscard]] RendererBackend backend() const override { return RendererBackend::D3D11; }

    bool attach_surface(NativeSurface& surface) override {
        (void)software_->attach_surface(surface);

        auto* hwnd = static_cast<HWND>(surface.native_handle());
        if (hwnd == nullptr) {
            NK_LOG_WARN("D3D11Renderer", "Win32 window handle is missing; D3D11 cannot attach");
            destroy_context();
            return false;
        }

        if (ready_ && hwnd_ == hwnd) {
            attached_surface_ = &surface;
            return true;
        }

        destroy_context();
        attached_surface_ = &surface;
        hwnd_ = hwnd;

        const UINT creation_flags = 0;
        const D3D_FEATURE_LEVEL requested_levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };
        HRESULT result = D3D11CreateDevice(nullptr,
                                           D3D_DRIVER_TYPE_HARDWARE,
                                           nullptr,
                                           creation_flags,
                                           requested_levels,
                                           static_cast<UINT>(std::size(requested_levels)),
                                           D3D11_SDK_VERSION,
                                           &device_,
                                           &feature_level_,
                                           &context_);
        if (result == E_INVALIDARG) {
            const D3D_FEATURE_LEVEL fallback_levels[] = {
                D3D_FEATURE_LEVEL_11_0,
            };
            result = D3D11CreateDevice(nullptr,
                                       D3D_DRIVER_TYPE_HARDWARE,
                                       nullptr,
                                       creation_flags,
                                       fallback_levels,
                                       static_cast<UINT>(std::size(fallback_levels)),
                                       D3D11_SDK_VERSION,
                                       &device_,
                                       &feature_level_,
                                       &context_);
        }
        if (FAILED(result)) {
            log_dxgi_failure("D3D11CreateDevice", result);
            destroy_context();
            return false;
        }

        IDXGIDevice* dxgi_device = nullptr;
        result = device_->QueryInterface(interface_iid<IDXGIDevice>(),
                                         reinterpret_cast<void**>(&dxgi_device));
        if (FAILED(result)) {
            log_dxgi_failure("IDXGIDevice::QueryInterface", result);
            safe_release(dxgi_device);
            destroy_context();
            return false;
        }

        IDXGIAdapter* adapter = nullptr;
        result = dxgi_device->GetAdapter(&adapter);
        safe_release(dxgi_device);
        if (FAILED(result)) {
            log_dxgi_failure("IDXGIDevice::GetAdapter", result);
            safe_release(adapter);
            destroy_context();
            return false;
        }

        result = adapter->GetParent(interface_iid<IDXGIFactory2>(),
                                    reinterpret_cast<void**>(&factory_));
        safe_release(adapter);
        if (FAILED(result)) {
            log_dxgi_failure("IDXGIAdapter::GetParent", result);
            destroy_context();
            return false;
        }

        const auto framebuffer = surface.framebuffer_size();
        if (!ensure_swapchain(framebuffer)) {
            destroy_context();
            return false;
        }

        ready_ = true;
        return true;
    }

    void set_text_shaper(TextShaper* shaper) override { software_->set_text_shaper(shaper); }

    void set_damage_regions(std::span<const Rect> regions) override {
        software_->set_damage_regions(regions);
        damage_regions_.clear();
        damage_regions_.reserve(regions.size());
        const auto framebuffer = attached_surface_ != nullptr ? attached_surface_->framebuffer_size() : Size{};
        const int width = std::max(1, static_cast<int>(std::lround(framebuffer.width)));
        const int height = std::max(1, static_cast<int>(std::lround(framebuffer.height)));
        for (const auto& region : regions) {
            const auto scaled = clamp_rect_to_framebuffer(region, width, height, scale_factor_);
            if (rect_is_empty(scaled)) {
                continue;
            }

            bool merged = false;
            for (auto& existing : damage_regions_) {
                if (rects_overlap_or_touch(existing, scaled)) {
                    existing = union_rects(existing, scaled);
                    merged = true;
                    break;
                }
            }
            if (!merged) {
                damage_regions_.push_back(scaled);
            }
        }
    }

    void begin_frame(Size viewport, float scale_factor) override {
        logical_viewport_ = viewport;
        scale_factor_ = normalize_scale_factor(scale_factor);
        software_->begin_frame(viewport, scale_factor_);
        last_hotspot_counters_ = {};
    }

    void render(const RenderNode& root) override { software_->render(root); }

    void end_frame() override {
        software_->end_frame();
        last_hotspot_counters_ = software_->last_hotspot_counters();
    }

    void present(NativeSurface& surface) override {
        if (!ready_ && !attach_surface(surface)) {
            software_->present(surface);
            last_hotspot_counters_ = software_->last_hotspot_counters();
            return;
        }

        const auto framebuffer = surface.framebuffer_size();
        if (!ensure_swapchain(framebuffer) || !ensure_upload_texture(framebuffer)) {
            software_->present(surface);
            last_hotspot_counters_ = software_->last_hotspot_counters();
            return;
        }

        const auto* rgba = software_->pixel_data();
        const int width = software_->pixel_width();
        const int height = software_->pixel_height();
        if (rgba == nullptr || width <= 0 || height <= 0) {
            return;
        }

        const bool full_redraw = damage_regions_.empty() || !upload_texture_initialized_;
        ensure_conversion_buffer(width, height);

        ID3D11Texture2D* backbuffer = nullptr;
        const HRESULT buffer_result =
            swap_chain_->GetBuffer(0, interface_iid<ID3D11Texture2D>(), reinterpret_cast<void**>(&backbuffer));
        if (FAILED(buffer_result) || backbuffer == nullptr) {
            log_dxgi_failure("IDXGISwapChain1::GetBuffer", buffer_result);
            safe_release(backbuffer);
            software_->present(surface);
            last_hotspot_counters_ = software_->last_hotspot_counters();
            return;
        }

        if (full_redraw) {
            convert_region_rgba_to_bgra(rgba, width, height, {0, 0, width, height});
            context_->UpdateSubresource(upload_texture_,
                                        0,
                                        nullptr,
                                        converted_pixels_.data(),
                                        static_cast<UINT>(width * 4),
                                        0);
            context_->CopyResource(backbuffer, upload_texture_);
            last_hotspot_counters_.gpu_present_path = GpuPresentPath::FullRedrawDirect;
            last_hotspot_counters_.gpu_present_tradeoff = GpuPresentTradeoff::None;
            last_hotspot_counters_.gpu_present_region_count = 0;
            last_hotspot_counters_.gpu_swapchain_copy_count = 1;
        } else {
            for (const auto& rect : damage_regions_) {
                const RECT clipped = {
                    std::clamp(static_cast<int>(rect.left), 0, width),
                    std::clamp(static_cast<int>(rect.top), 0, height),
                    std::clamp(static_cast<int>(rect.right), 0, width),
                    std::clamp(static_cast<int>(rect.bottom), 0, height),
                };
                if (rect_is_empty(clipped)) {
                    continue;
                }

                convert_region_rgba_to_bgra(rgba, width, height, clipped);
                const D3D11_BOX source_box = {
                    static_cast<UINT>(clipped.left),
                    static_cast<UINT>(clipped.top),
                    0,
                    static_cast<UINT>(clipped.right),
                    static_cast<UINT>(clipped.bottom),
                    1,
                };
                const auto* source = converted_pixels_.data() +
                                     static_cast<std::size_t>((clipped.top * width + clipped.left) * 4);
                context_->UpdateSubresource(upload_texture_,
                                            0,
                                            &source_box,
                                            source,
                                            static_cast<UINT>(width * 4),
                                            0);
                context_->CopySubresourceRegion(backbuffer,
                                               0,
                                               static_cast<UINT>(clipped.left),
                                               static_cast<UINT>(clipped.top),
                                               0,
                                               upload_texture_,
                                               0,
                                               &source_box);
            }

            last_hotspot_counters_.gpu_present_path = GpuPresentPath::PartialRedrawCopy;
            last_hotspot_counters_.gpu_present_tradeoff = GpuPresentTradeoff::BandwidthFavored;
            last_hotspot_counters_.gpu_present_region_count = damage_regions_.size();
            last_hotspot_counters_.gpu_swapchain_copy_count = damage_regions_.size();
        }

        upload_texture_initialized_ = true;
        context_->Flush();

        HRESULT present_result = S_OK;
        if (!full_redraw && !damage_regions_.empty()) {
            present_regions_.clear();
            present_regions_.reserve(damage_regions_.size());
            for (const auto& rect : damage_regions_) {
                if (!rect_is_empty(rect)) {
                    present_regions_.push_back(rect);
                }
            }

            DXGI_PRESENT_PARAMETERS present_parameters{};
            present_parameters.DirtyRectsCount = static_cast<UINT>(present_regions_.size());
            present_parameters.pDirtyRects = present_regions_.data();
            present_result = swap_chain_->Present1(1, 0, &present_parameters);
        } else {
            present_result = swap_chain_->Present(1, 0);
        }

        safe_release(backbuffer);

        if (FAILED(present_result) && present_result != DXGI_STATUS_OCCLUDED) {
            log_dxgi_failure("IDXGISwapChain1::Present1", present_result);
            if (present_result == DXGI_ERROR_DEVICE_REMOVED || present_result == DXGI_ERROR_DEVICE_RESET ||
                present_result == DXGI_ERROR_DEVICE_HUNG) {
                destroy_context();
            }
        }
    }

    [[nodiscard]] RenderHotspotCounters last_hotspot_counters() const override {
        return last_hotspot_counters_;
    }

private:
    bool ensure_swapchain(Size framebuffer_size) {
        const UINT width = std::max(1U, static_cast<UINT>(std::lround(framebuffer_size.width)));
        const UINT height = std::max(1U, static_cast<UINT>(std::lround(framebuffer_size.height)));
        if (factory_ == nullptr || device_ == nullptr || hwnd_ == nullptr) {
            return false;
        }

        if (swap_chain_ == nullptr) {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = width;
            desc.Height = height;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.Stereo = FALSE;
            desc.SampleDesc.Count = 1;
            desc.SampleDesc.Quality = 0;
            desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2;
            desc.Scaling = DXGI_SCALING_STRETCH;
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            desc.Flags = 0;

            const HRESULT result =
                factory_->CreateSwapChainForHwnd(device_, hwnd_, &desc, nullptr, nullptr, &swap_chain_);
            if (FAILED(result)) {
                log_dxgi_failure("IDXGIFactory2::CreateSwapChainForHwnd", result);
                return false;
            }

            framebuffer_width_ = width;
            framebuffer_height_ = height;
            upload_texture_initialized_ = false;
            return true;
        }

        if (framebuffer_width_ == width && framebuffer_height_ == height) {
            return true;
        }

        release_upload_texture();
        const HRESULT result = swap_chain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(result)) {
            log_dxgi_failure("IDXGISwapChain1::ResizeBuffers", result);
            return false;
        }

        framebuffer_width_ = width;
        framebuffer_height_ = height;
        upload_texture_initialized_ = false;
        return true;
    }

    bool ensure_upload_texture(Size framebuffer_size) {
        const UINT width = std::max(1U, static_cast<UINT>(std::lround(framebuffer_size.width)));
        const UINT height = std::max(1U, static_cast<UINT>(std::lround(framebuffer_size.height)));
        if (device_ == nullptr) {
            return false;
        }
        if (upload_texture_ != nullptr && width == upload_texture_width_ && height == upload_texture_height_) {
            return true;
        }

        release_upload_texture();

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = 0;
        desc.MiscFlags = 0;

        const HRESULT result = device_->CreateTexture2D(&desc, nullptr, &upload_texture_);
        if (FAILED(result)) {
            log_dxgi_failure("ID3D11Device::CreateTexture2D", result);
            return false;
        }

        upload_texture_width_ = width;
        upload_texture_height_ = height;
        upload_texture_initialized_ = false;
        return true;
    }

    void ensure_conversion_buffer(int width, int height) {
        const auto required_size =
            static_cast<std::size_t>(std::max(0, width)) * static_cast<std::size_t>(std::max(0, height)) * 4;
        if (converted_pixels_.size() != required_size) {
            converted_pixels_.resize(required_size, 0);
        }
    }

    void convert_region_rgba_to_bgra(const uint8_t* rgba, int width, int height, const RECT& region) {
        if (rgba == nullptr || width <= 0 || height <= 0) {
            return;
        }

        const int left = std::clamp(static_cast<int>(region.left), 0, width);
        const int top = std::clamp(static_cast<int>(region.top), 0, height);
        const int right = std::clamp(static_cast<int>(region.right), 0, width);
        const int bottom = std::clamp(static_cast<int>(region.bottom), 0, height);
        for (int y = top; y < bottom; ++y) {
            for (int x = left; x < right; ++x) {
                const auto index =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) *
                    4;
                converted_pixels_[index + 0] = rgba[index + 2];
                converted_pixels_[index + 1] = rgba[index + 1];
                converted_pixels_[index + 2] = rgba[index + 0];
                converted_pixels_[index + 3] = rgba[index + 3];
            }
        }
    }

    void release_upload_texture() {
        safe_release(upload_texture_);
        upload_texture_width_ = 0;
        upload_texture_height_ = 0;
        upload_texture_initialized_ = false;
    }

    void destroy_context() {
        attached_surface_ = nullptr;
        hwnd_ = nullptr;
        framebuffer_width_ = 0;
        framebuffer_height_ = 0;
        present_regions_.clear();
        damage_regions_.clear();
        release_upload_texture();
        safe_release(swap_chain_);
        safe_release(factory_);
        safe_release(context_);
        safe_release(device_);
        ready_ = false;
    }

    std::unique_ptr<SoftwareRenderer> software_;
    NativeSurface* attached_surface_ = nullptr;
    ID3D11Device* device_ = nullptr;
    ID3D11DeviceContext* context_ = nullptr;
    IDXGIFactory2* factory_ = nullptr;
    IDXGISwapChain1* swap_chain_ = nullptr;
    ID3D11Texture2D* upload_texture_ = nullptr;
    D3D_FEATURE_LEVEL feature_level_ = D3D_FEATURE_LEVEL_11_0;
    HWND hwnd_ = nullptr;
    Size logical_viewport_{};
    float scale_factor_ = 1.0F;
    UINT framebuffer_width_ = 0;
    UINT framebuffer_height_ = 0;
    UINT upload_texture_width_ = 0;
    UINT upload_texture_height_ = 0;
    std::vector<uint8_t> converted_pixels_;
    std::vector<RECT> damage_regions_;
    std::vector<RECT> present_regions_;
    RenderHotspotCounters last_hotspot_counters_{};
    bool ready_ = false;
    bool upload_texture_initialized_ = false;
};

} // namespace

std::unique_ptr<Renderer> create_d3d11_renderer() {
    return std::make_unique<D3D11Renderer>();
}

} // namespace nk

#endif
