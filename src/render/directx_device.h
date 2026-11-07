#pragma once

#include <array>
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <span>
#include <windows.h>
#include <wrl/client.h>

namespace nk {

// Private, UI-thread-owned presentation state shared by the DirectX renderers.
class DirectXDevice {
public:
    DirectXDevice() = default;
    ~DirectXDevice();
    DirectXDevice(const DirectXDevice&) = delete;
    DirectXDevice& operator=(const DirectXDevice&) = delete;
    bool create(HWND window, bool use_d3d12);
    bool resize(int width, int height);
    ID3D11Texture2D* acquire_back_buffer();
    bool present(std::span<RECT> dirty_rects);
    void reset();
    [[nodiscard]] bool healthy() const;
    [[nodiscard]] bool validation_failed() const;

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    Microsoft::WRL::ComPtr<IDXGISwapChain3> swap_chain;

private:
    bool wait_idle();
    bool release_buffers();
    Microsoft::WRL::ComPtr<ID3D12Device> device12_;
    Microsoft::WRL::ComPtr<ID3D12CommandQueue> queue_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    Microsoft::WRL::ComPtr<ID3D11On12Device> interop_;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, 2> buffers_;
    std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> resources12_;
    HANDLE fence_event_ = nullptr;
    UINT64 fence_value_ = 0;
    ID3D11Resource* acquired_ = nullptr;
    bool debug_ = false;
};

[[nodiscard]] bool directx12_available();

} // namespace nk
