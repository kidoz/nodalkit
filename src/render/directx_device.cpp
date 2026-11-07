#include "directx_device.h"

#include <cstdlib>
#include <d3d11sdklayers.h>
#include <d3d12sdklayers.h>
#include <limits>
#include <nk/foundation/logging.h>
#include <vector>

namespace nk {
namespace {
using Microsoft::WRL::ComPtr;

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wlanguage-extension-token"
#endif
template <typename T> const IID& type_iid() {
    return __uuidof(T);
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

bool create_d3d12_device(ComPtr<ID3D12Device>& device,
                         ComPtr<ID3D12CommandQueue>& queue,
                         ComPtr<ID3D11Device>& device11,
                         ComPtr<ID3D11DeviceContext>& context,
                         UINT flags) {
    if ((flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
        ComPtr<ID3D12Debug> debug;
        if (FAILED(D3D12GetDebugInterface(type_iid<ID3D12Debug>(),
                                          reinterpret_cast<void**>(debug.GetAddressOf())))) {
            return false;
        }
        debug->EnableDebugLayer();
    }
    if (FAILED(D3D12CreateDevice(nullptr,
                                 D3D_FEATURE_LEVEL_11_0,
                                 type_iid<ID3D12Device>(),
                                 reinterpret_cast<void**>(device.GetAddressOf())))) {
        return false;
    }
    D3D12_COMMAND_QUEUE_DESC desc{};
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&desc,
                                          type_iid<ID3D12CommandQueue>(),
                                          reinterpret_cast<void**>(queue.GetAddressOf())))) {
        return false;
    }
    IUnknown* queues[] = {queue.Get()};
    return SUCCEEDED(D3D11On12CreateDevice(
        device.Get(), flags, nullptr, 0, queues, 1, 0, &device11, &context, nullptr));
}
} // namespace

bool directx12_available() {
    static const bool available = [] {
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<ID3D11Device> device11;
        ComPtr<ID3D11DeviceContext> context;
        return create_d3d12_device(
            device, queue, device11, context, D3D11_CREATE_DEVICE_BGRA_SUPPORT);
    }();
    return available;
}

DirectXDevice::~DirectXDevice() {
    reset();
}

bool DirectXDevice::create(HWND window, bool use_d3d12) {
    reset();
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    debug_ = std::getenv("NK_DIRECTX_DEBUG") != nullptr;
    if (debug_) {
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    }
    if (use_d3d12) {
        if (!create_d3d12_device(device12_, queue_, device, context, flags) ||
            FAILED(device.As(&interop_)) ||
            FAILED(device12_->CreateFence(0,
                                          D3D12_FENCE_FLAG_NONE,
                                          type_iid<ID3D12Fence>(),
                                          reinterpret_cast<void**>(fence_.GetAddressOf())))) {
            reset();
            return false;
        }
        fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (fence_event_ == nullptr) {
            reset();
            return false;
        }
    } else {
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        HRESULT result = D3D11CreateDevice(nullptr,
                                           D3D_DRIVER_TYPE_HARDWARE,
                                           nullptr,
                                           flags,
                                           levels,
                                           2,
                                           D3D11_SDK_VERSION,
                                           &device,
                                           nullptr,
                                           &context);
        if (result == E_INVALIDARG) {
            result = D3D11CreateDevice(nullptr,
                                       D3D_DRIVER_TYPE_HARDWARE,
                                       nullptr,
                                       flags,
                                       levels + 1,
                                       1,
                                       D3D11_SDK_VERSION,
                                       &device,
                                       nullptr,
                                       &context);
        }
        if (FAILED(result)) {
            reset();
            return false;
        }
    }

    ComPtr<IDXGIFactory2> factory;
    if (FAILED(CreateDXGIFactory1(type_iid<IDXGIFactory2>(),
                                  reinterpret_cast<void**>(factory.GetAddressOf())))) {
        reset();
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    ComPtr<IDXGISwapChain1> chain;
    IUnknown* owner =
        use_d3d12 ? static_cast<IUnknown*>(queue_.Get()) : static_cast<IUnknown*>(device.Get());
    if (FAILED(factory->CreateSwapChainForHwnd(owner, window, &desc, nullptr, nullptr, &chain)) ||
        FAILED(chain.As(&swap_chain))) {
        reset();
        return false;
    }
    (void)factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER);
    return true;
}

bool DirectXDevice::healthy() const {
    return device != nullptr && swap_chain != nullptr &&
           SUCCEEDED(device->GetDeviceRemovedReason()) && !validation_failed();
}

bool DirectXDevice::validation_failed() const {
    if (!debug_ || device == nullptr) {
        return false;
    }
    ComPtr<ID3D11InfoQueue> messages11;
    if (SUCCEEDED(device.As(&messages11))) {
        for (UINT64 i = 0; i < messages11->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
            SIZE_T size = 0;
            if (FAILED(messages11->GetMessage(i, nullptr, &size))) {
                continue;
            }
            std::vector<char> storage(size);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            if (SUCCEEDED(messages11->GetMessage(i, message, &size)) &&
                message->Severity <= D3D11_MESSAGE_SEVERITY_ERROR) {
                NK_LOG_ERROR("DirectX", message->pDescription);
                return true;
            }
        }
    }
    ComPtr<ID3D12InfoQueue> messages12;
    if (device12_ != nullptr && SUCCEEDED(device12_.As(&messages12))) {
        for (UINT64 i = 0; i < messages12->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
            SIZE_T size = 0;
            if (FAILED(messages12->GetMessage(i, nullptr, &size))) {
                continue;
            }
            std::vector<char> storage(size);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(messages12->GetMessage(i, message, &size)) &&
                message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                NK_LOG_ERROR("DirectX", message->pDescription);
                return true;
            }
        }
    }
    return false;
}

bool DirectXDevice::wait_idle() {
    if (context != nullptr) {
        context->Flush();
    }
    if (queue_ == nullptr || fence_ == nullptr || fence_event_ == nullptr) {
        return true;
    }
    const auto value = ++fence_value_;
    if (FAILED(queue_->Signal(fence_.Get(), value))) {
        return false;
    }
    if (fence_->GetCompletedValue() == std::numeric_limits<UINT64>::max()) {
        return false;
    }
    if (fence_->GetCompletedValue() < value) {
        if (FAILED(fence_->SetEventOnCompletion(value, fence_event_))) {
            return false;
        }
        // Bound recovery if a driver stops making progress.
        return WaitForSingleObject(fence_event_, 5000) == WAIT_OBJECT_0 &&
               SUCCEEDED(device12_->GetDeviceRemovedReason());
    }
    return true;
}

bool DirectXDevice::release_buffers() {
    if (acquired_ != nullptr && interop_ != nullptr) {
        interop_->ReleaseWrappedResources(&acquired_, 1);
    }
    acquired_ = nullptr;
    if (context != nullptr) {
        context->ClearState();
    }
    for (auto& buffer : buffers_) {
        buffer.Reset();
    }
    // Wrapped resources retain D3D12 references until deferred destruction is flushed.
    const bool drained = wait_idle();
    for (auto& resource : resources12_) {
        resource.Reset();
    }
    return drained;
}

bool DirectXDevice::resize(int width, int height) {
    if (!healthy()) {
        return false;
    }
    if (!release_buffers()) {
        return false;
    }
    if (FAILED(swap_chain->ResizeBuffers(
            0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0))) {
        return false;
    }
    if (interop_ == nullptr) {
        return SUCCEEDED(swap_chain->GetBuffer(
            0, type_iid<ID3D11Texture2D>(), reinterpret_cast<void**>(buffers_[0].GetAddressOf())));
    }
    for (UINT i = 0; i < buffers_.size(); ++i) {
        D3D11_RESOURCE_FLAGS flags{};
        flags.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(
                swap_chain->GetBuffer(i,
                                      type_iid<ID3D12Resource>(),
                                      reinterpret_cast<void**>(resources12_[i].GetAddressOf()))) ||
            FAILED(interop_->CreateWrappedResource(
                resources12_[i].Get(),
                &flags,
                D3D12_RESOURCE_STATE_PRESENT,
                D3D12_RESOURCE_STATE_PRESENT,
                type_iid<ID3D11Texture2D>(),
                reinterpret_cast<void**>(buffers_[i].GetAddressOf())))) {
            release_buffers();
            return false;
        }
        ID3D11Resource* resource = buffers_[i].Get();
        interop_->ReleaseWrappedResources(&resource, 1);
    }
    context->Flush();
    return true;
}

ID3D11Texture2D* DirectXDevice::acquire_back_buffer() {
    if (!healthy()) {
        return nullptr;
    }
    if (interop_ == nullptr) {
        return buffers_[0].Get();
    }
    auto* buffer = buffers_[swap_chain->GetCurrentBackBufferIndex()].Get();
    if (buffer != nullptr && acquired_ == nullptr) {
        acquired_ = buffer;
        interop_->AcquireWrappedResources(&acquired_, 1);
    }
    return buffer;
}

bool DirectXDevice::present(std::span<RECT> dirty_rects) {
    if (interop_ != nullptr && acquired_ != nullptr) {
        interop_->ReleaseWrappedResources(&acquired_, 1);
        acquired_ = nullptr;
        context->Flush();
    }
    DXGI_PRESENT_PARAMETERS parameters{};
    parameters.DirtyRectsCount = static_cast<UINT>(dirty_rects.size());
    parameters.pDirtyRects = dirty_rects.data();
    return swap_chain != nullptr && SUCCEEDED(swap_chain->Present1(1, 0, &parameters));
}

void DirectXDevice::reset() {
    (void)release_buffers();
    swap_chain.Reset();
    interop_.Reset();
    context.Reset();
    device.Reset();
    fence_.Reset();
    queue_.Reset();
    device12_.Reset();
    if (fence_event_ != nullptr) {
        CloseHandle(fence_event_);
        fence_event_ = nullptr;
    }
    fence_value_ = 0;
    debug_ = false;
}
} // namespace nk
