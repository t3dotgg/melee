#include "native_swapchain.h"

#include <algorithm>
#include <cstring>
#include <type_traits>

#ifdef _WIN32
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <windows.h>
#include <wrl/client.h>

namespace {
constexpr wchar_t kClassName[] = L"MeleeNativeSwapChainWindow";

LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                         reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    if (msg == WM_CLOSE) {
        auto* close_requested = reinterpret_cast<bool*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (close_requested != nullptr) *close_requested = true;
        return 0; // The owning loop stops before releasing GPU/window resources.
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

bool compile_shader(HMODULE module, const char* source, const char* entry, const char* profile,
                    ID3DBlob** bytecode) noexcept
{
    using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*,
                                       ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT,
                                       ID3DBlob**, ID3DBlob**);
    auto compile = reinterpret_cast<CompileFn>(GetProcAddress(module, "D3DCompile"));
    if (compile == nullptr) return false;
    ID3DBlob* errors = nullptr;
    const HRESULT result = compile(source, std::strlen(source), "melee_native_shader", nullptr,
                                    nullptr, entry, profile, 0, 0, bytecode, &errors);
    if (errors != nullptr) errors->Release();
    return SUCCEEDED(result);
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
    d3dcompiler_module_ = LoadLibraryW(L"d3dcompiler_47.dll");
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
                              instance, &close_requested_);
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
    if (d3dcompiler_module_ != nullptr) {
        constexpr char shader[] = R"(
struct VIn { float3 p : POSITION; float4 c : COLOR; };
struct VOut { float4 p : SV_Position; float4 c : COLOR; };
VOut vs_main(VIn i) { VOut o; o.p=float4(i.p.x/10.0, i.p.y/6.0, 0.0, 1.0); o.c=i.c; return o; }
float4 ps_main(VOut i) : SV_Target { return i.c; }
)";
        ID3DBlob* vs = nullptr;
        ID3DBlob* ps = nullptr;
        if (compile_shader(static_cast<HMODULE>(d3dcompiler_module_), shader, "vs_main", "vs_5_0", &vs) &&
            compile_shader(static_cast<HMODULE>(d3dcompiler_module_), shader, "ps_main", "ps_5_0", &ps)) {
            D3D12_ROOT_SIGNATURE_DESC root_desc{};
            root_desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
            ID3DBlob* root_blob = nullptr;
            ID3DBlob* root_error = nullptr;
            if (SUCCEEDED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1,
                                                       &root_blob, &root_error)) &&
                SUCCEEDED(device->CreateRootSignature(0, root_blob->GetBufferPointer(),
                                                       root_blob->GetBufferSize(),
                                                       __uuidof(ID3D12RootSignature), &root_signature_))) {
                D3D12_INPUT_ELEMENT_DESC input[] = {
                    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12,
                     D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
                };
                D3D12_GRAPHICS_PIPELINE_STATE_DESC pso{};
                pso.InputLayout = {input, 2};
                pso.pRootSignature = static_cast<ID3D12RootSignature*>(root_signature_);
                pso.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
                pso.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
                pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
                pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
                pso.RasterizerState.DepthClipEnable = TRUE;
                pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
                pso.SampleMask = UINT_MAX;
                pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
                pso.NumRenderTargets = 1;
                pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
                pso.SampleDesc.Count = 1;
                device->CreateGraphicsPipelineState(&pso, __uuidof(ID3D12PipelineState), &pipeline_state_);
            }
            if (root_error != nullptr) root_error->Release();
            if (root_blob != nullptr) root_blob->Release();
        }
        if (vs != nullptr) vs->Release();
        if (ps != nullptr) ps->Release();
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
    // A failed signal/wait can leave the last submission outstanding. Retire
    // that work before releasing storage, unless the device itself was removed.
    if (queue_ != nullptr && fence_ != nullptr && fence_event_ != nullptr && submission_failed_) {
        auto* fence = static_cast<ID3D12Fence*>(fence_);
        const std::uint64_t value = ++fence_value_;
        if (SUCCEEDED(static_cast<ID3D12CommandQueue*>(queue_)->Signal(fence, value)) &&
            fence->GetCompletedValue() < value &&
            SUCCEEDED(fence->SetEventOnCompletion(value, static_cast<HANDLE>(fence_event_))))
            WaitForSingleObject(static_cast<HANDLE>(fence_event_), INFINITE);
    }
    if (geometry_upload_ != nullptr && geometry_mapping_ != nullptr)
        static_cast<ID3D12Resource*>(geometry_upload_)->Unmap(0, nullptr);
    release_object(geometry_upload_);
    release_object(geometry_buffer_);
    release_render_targets(back_buffers_);
    release_object(fence_);
    release_object(pipeline_state_);
    release_object(root_signature_);
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
    if (d3dcompiler_module_ != nullptr) FreeLibrary(static_cast<HMODULE>(d3dcompiler_module_));
    if (com_initialized_) CoUninitialize();
#endif
    swap_chain_ = factory_ = queue_ = device_ = window_ = nullptr;
    d3d12_module_ = dxgi_module_ = nullptr;
    d3dcompiler_module_ = nullptr;
    rtv_increment_ = 0;
    fence_value_ = 0;
    width_ = height_ = buffer_count_ = 0;
    geometry_upload_ = geometry_buffer_ = geometry_mapping_ = nullptr;
    geometry_plan_ = {};
    geometry_draws_.clear();
    geometry_capacity_ = 0;
    geometry_uploads_ = 0;
    geometry_pending_ = geometry_buffer_ready_ = submission_failed_ = false;
    close_requested_ = false;
    tearing_supported_ = class_registered_ = com_initialized_ = false;
    presented_frames_ = 0;
    draw_calls_ = 0;
}

bool NativeWin32SwapChain::prepare_geometry(const NativeRenderGeometry& geometry) noexcept
{
    NativeGeometryUploadPlan plan;
    if (!available() || submission_failed_ || !plan_geometry_upload(geometry, plan)) return false;
#ifdef _WIN32
    static_assert(std::is_trivially_copyable_v<NativeRenderVertex>);
    static_assert(sizeof(NativeRenderVertex) == 16);
    // Complete all allocating work before changing the last accepted frame.
    std::vector<NativeRenderDraw> draws;
    try {
        draws = geometry.draws;
    } catch (...) {
        return false;
    }
    if (plan.total_bytes > geometry_capacity_) {
        std::uint32_t capacity = 65536;
        while (capacity < plan.total_bytes) capacity *= 2;
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = capacity;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        auto* device = static_cast<ID3D12Device*>(device_);
        ID3D12Resource* upload = nullptr;
        ID3D12Resource* buffer = nullptr;
        void* mapping = nullptr;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                    __uuidof(ID3D12Resource),
                                                    reinterpret_cast<void**>(&upload))))
            return false;
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        const D3D12_RANGE no_reads{0, 0};
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    __uuidof(ID3D12Resource),
                                                    reinterpret_cast<void**>(&buffer))) ||
            FAILED(upload->Map(0, &no_reads, &mapping))) {
            if (buffer != nullptr) buffer->Release();
            upload->Release();
            return false;
        }
        if (geometry_upload_ != nullptr)
            static_cast<ID3D12Resource*>(geometry_upload_)->Unmap(0, nullptr);
        release_object(geometry_upload_);
        release_object(geometry_buffer_);
        geometry_upload_ = upload;
        geometry_buffer_ = buffer;
        geometry_mapping_ = mapping;
        geometry_capacity_ = capacity;
        geometry_buffer_ready_ = false;
    }
    if (plan.total_bytes != 0) {
        auto* destination = static_cast<std::byte*>(geometry_mapping_);
        std::memcpy(destination, geometry.vertices.data(), plan.vertex_bytes);
        std::memset(destination + plan.vertex_bytes, 0, plan.index_offset - plan.vertex_bytes);
        std::memcpy(destination + plan.index_offset, geometry.indices.data(), plan.index_bytes);
    }
    geometry_plan_ = plan;
    geometry_draws_.swap(draws);
    geometry_pending_ = plan.total_bytes != 0;
    return true;
#else
    return false;
#endif
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
                                             float alpha, bool capture) noexcept
{
#ifdef _WIN32
    if (swap_chain_ == nullptr || command_allocator_ == nullptr || command_list_ == nullptr ||
        rtv_heap_ == nullptr || fence_ == nullptr || fence_event_ == nullptr || submission_failed_)
        return false;
    auto* chain = static_cast<IDXGISwapChain3*>(swap_chain_);
    auto* allocator = static_cast<ID3D12CommandAllocator*>(command_allocator_);
    auto* list = static_cast<ID3D12GraphicsCommandList*>(command_list_);
    auto* queue = static_cast<ID3D12CommandQueue*>(queue_);
    auto* fence = static_cast<ID3D12Fence*>(fence_);
    const UINT index = chain->GetCurrentBackBufferIndex();
    if (index >= buffer_count_ || back_buffers_[index] == nullptr) return false;
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT64 readback_bytes = 0;
    if (capture) {
        auto* device = static_cast<ID3D12Device*>(device_);
        const auto target_desc = static_cast<ID3D12Resource*>(back_buffers_[index])->GetDesc();
        device->GetCopyableFootprints(&target_desc, 0, 1, 0, &footprint, nullptr, nullptr,
                                      &readback_bytes);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = readback_bytes;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return false;
        try { captured_pixels_.resize(static_cast<std::size_t>(width_) * height_ * 4); }
        catch (...) { return false; }
    }
    if (FAILED(allocator->Reset()) || FAILED(list->Reset(allocator, nullptr))) return false;
    constexpr D3D12_RESOURCE_STATES geometry_read = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER |
                                                  D3D12_RESOURCE_STATE_INDEX_BUFFER;
    if (geometry_pending_) {
        auto* buffer = static_cast<ID3D12Resource*>(geometry_buffer_);
        D3D12_RESOURCE_BARRIER copy_barrier{};
        copy_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        copy_barrier.Transition.pResource = buffer;
        copy_barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        copy_barrier.Transition.StateBefore = geometry_read;
        copy_barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        if (geometry_buffer_ready_) list->ResourceBarrier(1, &copy_barrier);
        list->CopyBufferRegion(buffer, 0, static_cast<ID3D12Resource*>(geometry_upload_), 0,
                               geometry_plan_.total_bytes);
        std::swap(copy_barrier.Transition.StateBefore, copy_barrier.Transition.StateAfter);
        list->ResourceBarrier(1, &copy_barrier);
    }
    if (geometry_plan_.total_bytes != 0) {
        const auto address = static_cast<ID3D12Resource*>(geometry_buffer_)->GetGPUVirtualAddress();
        const D3D12_VERTEX_BUFFER_VIEW vertices{address, geometry_plan_.vertex_bytes,
                                              sizeof(NativeRenderVertex)};
        const D3D12_INDEX_BUFFER_VIEW indices{address + geometry_plan_.index_offset,
                                            geometry_plan_.index_bytes, DXGI_FORMAT_R32_UINT};
        list->IASetVertexBuffers(0, 1, &vertices);
        list->IASetIndexBuffer(&indices);
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }
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
    list->OMSetRenderTargets(1, &descriptor, FALSE, nullptr);
    const D3D12_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(width_),
                                  static_cast<float>(height_), 0.0F, 1.0F};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
    list->RSSetViewports(1, &viewport);
    list->RSSetScissorRects(1, &scissor);
    if (pipeline_state_ != nullptr && geometry_plan_.total_bytes != 0) {
        list->SetGraphicsRootSignature(static_cast<ID3D12RootSignature*>(root_signature_));
        list->SetPipelineState(static_cast<ID3D12PipelineState*>(pipeline_state_));
        for (const auto& draw : geometry_draws_) {
            list->DrawIndexedInstanced(draw.index_count, 1, draw.first_index, 0, 0);
            ++draw_calls_;
        }
    }
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (capture) {
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION source{};
        source.pResource = resource;
        source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION destination{};
        destination.pResource = readback.Get();
        destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        destination.PlacedFootprint = footprint;
        list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    list->ResourceBarrier(1, &barrier);
    if (FAILED(list->Close())) return false;
    ID3D12CommandList* lists[] = {list};
    queue->ExecuteCommandLists(1, lists);
    submission_failed_ = true; // Do not reuse storage until the fence completes.
    const std::uint64_t value = ++fence_value_;
    if (FAILED(queue->Signal(fence, value))) return false;
    if (fence->GetCompletedValue() < value) {
        if (FAILED(fence->SetEventOnCompletion(value, static_cast<HANDLE>(fence_event_))))
            return false;
        if (WaitForSingleObject(static_cast<HANDLE>(fence_event_), INFINITE) != WAIT_OBJECT_0)
            return false;
    }
    if (fence->GetCompletedValue() == UINT64_MAX) return false; // Removed device.
    submission_failed_ = false;
    if (capture) {
        void* pixels = nullptr;
        const D3D12_RANGE range{0, static_cast<SIZE_T>(readback_bytes)};
        if (FAILED(readback->Map(0, &range, &pixels))) return false;
        for (std::uint32_t y = 0; y < height_; ++y) {
            std::memcpy(captured_pixels_.data() + static_cast<std::size_t>(y) * width_ * 4,
                        static_cast<const std::byte*>(pixels) + footprint.Offset +
                            static_cast<std::size_t>(y) * footprint.Footprint.RowPitch,
                        static_cast<std::size_t>(width_) * 4);
        }
        const D3D12_RANGE no_writes{0, 0};
        readback->Unmap(0, &no_writes);
    }
    if (geometry_pending_) {
        ++geometry_uploads_;
        geometry_pending_ = false;
        geometry_buffer_ready_ = true;
    }
    return present();
#else
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
    (void)capture;
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








