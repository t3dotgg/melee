#include "native_windows_input.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace melee::native {

WindowsXInputDevice::WindowsXInputDevice()
{
#ifdef _WIN32
    constexpr const wchar_t* names[] = {L"xinput1_4.dll", L"xinput1_3.dll"};
    for (const auto* name : names) {
        module_ = LoadLibraryW(name);
        if (module_ != nullptr) {
            query_ = reinterpret_cast<Query>(GetProcAddress(static_cast<HMODULE>(module_), "XInputGetState"));
            if (query_ != nullptr) break;
            FreeLibrary(static_cast<HMODULE>(module_));
            module_ = nullptr;
        }
    }
#endif
}

WindowsXInputDevice::~WindowsXInputDevice()
{
#ifdef _WIN32
    if (module_ != nullptr) FreeLibrary(static_cast<HMODULE>(module_));
#endif
}

bool WindowsXInputDevice::read(std::uint32_t user_index, XInputState& state) const noexcept
{
    if (query_ == nullptr || user_index >= 4) return false;
#ifdef _WIN32
    struct RawState {
        std::uint32_t packet_number;
        XInputState gamepad;
    } raw{};
    if (query_(user_index, &raw) != 0) return false;
    state = raw.gamepad;
    return true;
#else
    (void)user_index;
    (void)state;
    return false;
#endif
}

} // namespace melee::native
