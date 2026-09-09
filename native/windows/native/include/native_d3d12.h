#pragma once

#include <cstdint>

namespace melee::native {

// Owns only the device object; swap-chain/window ownership belongs to the
// frontend. The loader is dynamic so headless systems can use the null backend.
class NativeD3D12Device final {
public:
    NativeD3D12Device() = default;
    ~NativeD3D12Device();
    NativeD3D12Device(const NativeD3D12Device&) = delete;
    NativeD3D12Device& operator=(const NativeD3D12Device&) = delete;

    bool initialize();
    void shutdown() noexcept;
    bool available() const noexcept { return device_ != nullptr; }
    std::uint32_t feature_level() const noexcept { return feature_level_; }

private:
    void* module_ = nullptr;
    void* device_ = nullptr;
    std::uint32_t feature_level_ = 0;
};

} // namespace melee::native
