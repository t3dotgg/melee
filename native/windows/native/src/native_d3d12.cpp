#include "native_d3d12.h"

#ifdef _WIN32
#include <d3d12.h>
#include <windows.h>
#endif

namespace melee::native {

NativeD3D12Device::~NativeD3D12Device() { shutdown(); }

bool NativeD3D12Device::initialize()
{
#ifdef _WIN32
    shutdown();
    module_ = LoadLibraryW(L"d3d12.dll");
    if (module_ == nullptr) return false;
    using CreateDevice = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
    const auto create = reinterpret_cast<CreateDevice>(
        GetProcAddress(static_cast<HMODULE>(module_), "D3D12CreateDevice"));
    if (create == nullptr) { shutdown(); return false; }
    constexpr D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    for (const auto level : levels) {
        void* device = nullptr;
        if (SUCCEEDED(create(nullptr, level, __uuidof(ID3D12Device), &device))) {
            device_ = device;
            feature_level_ = static_cast<std::uint32_t>(level);
            return true;
        }
    }
    shutdown();
#endif
    return false;
}

void NativeD3D12Device::shutdown() noexcept
{
#ifdef _WIN32
    if (device_ != nullptr) static_cast<IUnknown*>(device_)->Release();
    if (module_ != nullptr) FreeLibrary(static_cast<HMODULE>(module_));
#endif
    device_ = nullptr;
    module_ = nullptr;
    feature_level_ = 0;
}

} // namespace melee::native
