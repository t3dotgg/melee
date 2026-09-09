#include "native_renderer_backend.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace melee::native {

NativeD3D12Probe::NativeD3D12Probe()
{
#ifdef _WIN32
    module_ = LoadLibraryW(L"d3d12.dll");
    if (module_ != nullptr)
        create_device_ = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module_), "D3D12CreateDevice"));
#endif
}

NativeD3D12Probe::~NativeD3D12Probe()
{
#ifdef _WIN32
    if (module_ != nullptr) FreeLibrary(static_cast<HMODULE>(module_));
#endif
}

} // namespace melee::native
