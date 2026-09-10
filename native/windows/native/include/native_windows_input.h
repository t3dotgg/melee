#pragma once

#include "native_input.h"

#include <cstdint>
#include <memory>

namespace melee::native {

// Runtime-loaded XInput adapter. Loading the DLL dynamically keeps the native
// core testable on machines without an XInput redistributable while providing
// real controller samples on Windows.
class WindowsXInputDevice final {
public:
    WindowsXInputDevice();
    ~WindowsXInputDevice();
    WindowsXInputDevice(const WindowsXInputDevice&) = delete;
    WindowsXInputDevice& operator=(const WindowsXInputDevice&) = delete;

    bool available() const noexcept { return query_ != nullptr; }
    bool read(std::uint32_t user_index, XInputState& state) const noexcept;

private:
    using Query = unsigned long(__stdcall*)(unsigned long, void*);
    void* module_ = nullptr;
    Query query_ = nullptr;
};

} // namespace melee::native
