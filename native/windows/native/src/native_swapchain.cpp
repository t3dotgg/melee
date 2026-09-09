#include "native_swapchain.h"

#ifdef _WIN32
#include <d3d12.h>
#include <dxgi1_6.h>
#include <windows.h>

#include <algorithm>

namespace {
constexpr wchar_t kClassName[] = L"MeleeNativeSwapChainWindow";

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_CLOSE) {
        return 0; // hidden frontend owns lifetime; don't destroy from a stray close
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void release_object(void*& ptr) noexcept
{
    if (ptr != nullptr) static_cast<IUnknown*>(ptr)->Release();
    ptr = nullptr;
}

void release_render_targets(std::array<void*, 8>& targets) noexcept
{
    for (auto& target : targets) release_object(target);
}
} // namespace
#endif

namespace melee::native {

NativeWin32SwapChain::~NativeWin32SwapChain() { shutdown(); }

bool NativeWin32SwapChain::initialize(const NativeSwapChainDesc& desc)
{
    shutdown();
    width_ = std::max<std::uint32_t>(1, desc.width);
    height_ = std::max<std::uint32_t>(1, desc.height);
    buffer_count_ = std::clamp<std::uint32_t>(desc.buffer_count, 2, 8);
#ifdef _WIN32
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(co)) com_initialized_ = true;
    else if (co != RPC_E_CHANGED_MODE) return false;

    auto fail = [this]() {
        shutdown();
        return false;
    };
    d3d12_module_ = LoadLibraryW(L"d3d12.dll");
    dxgi_module_ = LoadLibraryW(L"dxgi.dll");
    if (d3d12_module_ == nullptr || dxgi_module_ == nullptr) return fail();
    using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    using CreateFactoryFn = HRESULT(WINAPI*)(UINT, REFIID, void**);
    const auto create_device = reinterpret_cast<CreateDeviceFn>(
        GetProcAddress(static_cast<HMODULE>(d3d12_module_), "D3D12CreateDevice"));
    const auto create_factory = reinterpret_cast<CreateFactoryFn>(
        GetProcAddress(static_cast<HMODULE>(dxgi_module_), "CreateDXGIFactory2"));
    if (create_device == nullptr || create_factory == nullptr) return fail();

    constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1,
                                            D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
                                            D3D_FEATURE_LEVEL_11_0};
    for (const auto level : levels) {
        if (SUCCEEDED(create_device(nullptr, level, __uuidof(ID3D12Device), &device_))) break;
    }
    if (device_ == nullptr) return fail();
    auto* device = static_cast<ID3D12Device*>(device_);
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(device->CreateCommandQueue(&queue_desc, __uuidof(ID3D12CommandQueue), &queue_)))
        return fail();

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.hInstance = instance;
    wc.lpfnWndProc = window_proc;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return fail();
    class_registered_ = true;
    const DWORD style = WS_OVERLAPPEDWINDOW;
    window_ = CreateWindowExW(0, kClassName, L"Melee Native", style, 0, 0,
                              static_cast<int>(width_), static_cast<int>(height_), nullptr, nullptr,
                              instance, nullptr);
    if (window_ == nullptr) return fail();

    auto* factory = static_cast<IDXGIFactory4*>(nullptr);
    if (FAILED(create_factory(0, __uuidof(IDXGIFactory4), reinterpret_cast<void**>(&factory))))
        return fail();
    factory_ = factory;
    DXGI_SWAP_CHAIN_DESC1 swap_desc{};
    swap_desc.Width = width_;
    swap_desc.Height = height_;
    swap_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_desc.BufferCount = buffer_count_;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swap_desc.Scaling = DXGI_SCALING_STRETCH;
    swap_desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    BOOL allow_tearing = FALSE;
    IDXGIFactory5* factory5 = nullptr;
    if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory5), reinterpret_cast<void**>(&factory5)))) {
        if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing))))
            tearing_supported_ = desc.allow_tearing && allow_tearing != FALSE;
        factory5->Release();
    }
    if (tearing_supported_) swap_desc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
    IDXGISwapChain1* chain = nullptr;
    if (FAILED(factory->CreateSwapChainForHwnd(static_cast<ID3D12CommandQueue*>(queue_),
                                                static_cast<HWND>(window_), &swap_desc, nullptr,
                                                nullptr, &chain)))
        return fail();
    if (FAILED(chain->QueryInterface(__uuidof(IDXGISwapChain3), &swap_chain_))) {
        chain->Release();
        return fail();
    }
    chain->Release();
    factory->MakeWindowAssociation(static_cast<HWND>(window_), DXGI_MWA_NO_ALT_ENTER);

    // Allocate the objects required by the first real native render pass.
    D3D12_DESCRIPTOR_HEAP_DESC rtv_desc{};
    rtv_desc.NumDescriptors = buffer_count_;
    rtv_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    if (FAILED(device->CreateDescriptorHeap(&rtv_desc, __uuidof(ID3D12DescriptorHeap),
                                            &rtv_heap_)))
        return fail();
    rtv_increment_ = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    if (rtv_increment_ == 0) return fail();
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                               __uuidof(ID3D12CommandAllocator),
                                               &command_allocator_)))
        return fail();
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         static_cast<ID3D12CommandAllocator*>(command_allocator_),
                                         nullptr, __uuidof(ID3D12GraphicsCommandList),
                                         &command_list_)))
        return fail();
    // New command lists start in the recording state; close until first use.
    if (FAILED(static_cast<ID3D12GraphicsCommandList*>(command_list_)->Close())) return fail();
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), &fence_)))
        return fail();
    fence_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (fence_event_ == nullptr) return fail();
    auto* chain3 = static_cast<IDXGISwapChain3*>(swap_chain_);
    auto descriptor = static_cast<ID3D12DescriptorHeap*>(rtv_heap_)->GetCPUDescriptorHandleForHeapStart();
    for (std::uint32_t i = 0; i < buffer_count_; ++i) {
        if (FAILED(chain3->GetBuffer(i, __uuidof(ID3D12Resource), &back_buffers_[i]))) return fail();
        D3D12_CPU_DESCRIPTOR_HANDLE handle = descriptor;
        handle.ptr += static_cast<SIZE_T>(i) * rtv_increment_;
        device->CreateRenderTargetView(static_cast<ID3D12Resource*>(back_buffers_[i]), nullptr, handle);
    }
    return true;
#else
    (void)desc;
    return false;
#endif
}

void NativeWin32SwapChain::shutdown() noexcept
{
#ifdef _WIN32
    release_render_targets(back_buffers_);
    release_object(fence_);
    release_object(rtv_heap_);
    release_object(command_list_);
    release_object(command_allocator_);
    if (fence_event_ != nullptr) CloseHandle(static_cast<HANDLE>(fence_event_));
    fence_event_ = nullptr;
    release_object(swap_chain_);
    release_object(factory_);
    release_object(queue_);
    release_object(device_);
    if (window_ != nullptr) DestroyWindow(static_cast<HWND>(window_));
    if (class_registered_) UnregisterClassW(kClassName, GetModuleHandleW(nullptr));
    if (dxgi_module_ != nullptr) FreeLibrary(static_cast<HMODULE>(dxgi_module_));
    if (d3d12_module_ != nullptr) FreeLibrary(static_cast<HMODULE>(d3d12_module_));
    if (com_initialized_) CoUninitialize();
#endif
    swap_chain_ = factory_ = queue_ = device_ = window_ = nullptr;
    d3d12_module_ = dxgi_module_ = nullptr;
    rtv_increment_ = 0;
    fence_value_ = 0;
    width_ = height_ = buffer_count_ = 0;
    tearing_supported_ = class_registered_ = com_initialized_ = false;
    presented_frames_ = 0;
}

bool NativeWin32SwapChain::present() noexcept
{
#ifdef _WIN32
    if (swap_chain_ == nullptr) return false;
    UINT flags = tearing_supported_ ? DXGI_PRESENT_ALLOW_TEARING : 0;
    const HRESULT hr = static_cast<IDXGISwapChain3*>(swap_chain_)->Present(0, flags);
    if (SUCCEEDED(hr)) {
        ++presented_frames_;
        return true;
    }
#endif
    return false;
}

bool NativeWin32SwapChain::clear_and_present(float red, float green, float blue,
                                             float alpha) noexcept
{
#ifdef _WIN32
    if (swap_chain_ == nullptr || command_allocator_ == nullptr || command_list_ == nullptr ||
        rtv_heap_ == nullptr || fence_ == nullptr || fence_event_ == nullptr)
        return false;
    auto* chain = static_cast<IDXGISwapChain3*>(swap_chain_);
    auto* allocator = static_cast<ID3D12CommandAllocator*>(command_allocator_);
    auto* list = static_cast<ID3D12GraphicsCommandList*>(command_list_);
    auto* queue = static_cast<ID3D12CommandQueue*>(queue_);
    auto* fence = static_cast<ID3D12Fence*>(fence_);
    const UINT index = chain->GetCurrentBackBufferIndex();
    if (index >= buffer_count_ || back_buffers_[index] == nullptr) return false;
    if (FAILED(allocator->Reset()) || FAILED(list->Reset(allocator, nullptr))) return false;
    auto* resource = static_cast<ID3D12Resource*>(back_buffers_[index]);
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    list->ResourceBarrier(1, &barrier);
    auto descriptor = static_cast<ID3D12DescriptorHeap*>(rtv_heap_)->GetCPUDescriptorHandleForHeapStart();
    descriptor.ptr += static_cast<SIZE_T>(index) * rtv_increment_;
    const float clear_color[4] = {red, green, blue, alpha};
    list->ClearRenderTargetView(descriptor, clear_color, 0, nullptr);
    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    list->ResourceBarrier(1, &barrier);
    if (FAILED(list->Close())) return false;
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    const std::uint64_t value = ++fence_value_;
    if (FAILED(queue->Signal(fence, value))) return false;
    if (fence->GetCompletedValue() < value) {
        if (FAILED(fence->SetEventOnCompletion(value, static_cast<HANDLE>(fence_event_))))
            return false;
        if (WaitForSingleObject(static_cast<HANDLE>(fence_event_), INFINITE) != WAIT_OBJECT_0)
            return false;
    }
    return present();
#else
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
    return false;
#endif
}

void NativeWin32SwapChain::pump_messages() noexcept
{
#ifdef _WIN32
    MSG msg;
    while (PeekMessageW(&msg, static_cast<HWND>(window_), 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
#endif
}

void NativeWin32SwapChain::show() noexcept
{
#ifdef _WIN32
    if (window_ != nullptr) ShowWindow(static_cast<HWND>(window_), SW_SHOW);
#endif
}

} // namespace melee::native








