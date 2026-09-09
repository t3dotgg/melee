#include "native_swapchain.h"

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <windows.h>
#endif

namespace melee::native {

NativeSwapChain::~NativeSwapChain() { shutdown(); }

bool NativeSwapChain::initialize(const NativeWindow& window, NativeD3D12Device& device,
                                 std::uint32_t buffer_count)
{
#ifdef _WIN32
    shutdown();
    if (!window.valid() || !device.available() || buffer_count < 2 || buffer_count > 4) return false;
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory4> factory;
    if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    auto* d3d_device = static_cast<ID3D12Device*>(device.native_handle());
    if (FAILED(d3d_device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue)))) return false;
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = window.width(); desc.Height = window.height();
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; desc.BufferCount = buffer_count;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    ComPtr<IDXGISwapChain1> swap;
    if (FAILED(factory->CreateSwapChainForHwnd(queue.Get(), static_cast<HWND>(window.handle()),
                                               &desc, nullptr, nullptr, &swap))) return false;
    factory->MakeWindowAssociation(static_cast<HWND>(window.handle()), DXGI_MWA_NO_ALT_ENTER);
    factory_ = factory.Detach(); queue_ = queue.Detach(); swap_chain_ = swap.Detach();
    return true;
#else
    (void)window; (void)device; (void)buffer_count;
    return false;
#endif
}

void NativeSwapChain::shutdown() noexcept
{
#ifdef _WIN32
    if (swap_chain_ != nullptr) static_cast<IUnknown*>(swap_chain_)->Release();
    if (queue_ != nullptr) static_cast<IUnknown*>(queue_)->Release();
    if (factory_ != nullptr) static_cast<IUnknown*>(factory_)->Release();
#endif
    swap_chain_ = nullptr; queue_ = nullptr; factory_ = nullptr; present_count_ = 0;
}

bool NativeSwapChain::present() noexcept
{
#ifdef _WIN32
    if (swap_chain_ == nullptr || FAILED(static_cast<IDXGISwapChain1*>(swap_chain_)->Present(0, 0))) return false;
    ++present_count_;
    return true;
#else
    return false;
#endif
}

} // namespace melee::native
