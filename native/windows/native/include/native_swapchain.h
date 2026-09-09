#pragma once

#include "native_d3d12.h"
#include "native_window.h"

#include <cstdint>
#include <memory>

namespace melee::native {

class NativeSwapChain final {
public:
    NativeSwapChain() = default;
    ~NativeSwapChain();
    NativeSwapChain(const NativeSwapChain&) = delete;
    NativeSwapChain& operator=(const NativeSwapChain&) = delete;

    bool initialize(const NativeWindow& window, NativeD3D12Device& device,
                    std::uint32_t buffer_count = 2);
    void shutdown() noexcept;
    bool available() const noexcept { return swap_chain_ != nullptr; }
    bool present() noexcept;
    std::uint64_t present_count() const noexcept { return present_count_; }

private:
    void* factory_ = nullptr;
    void* queue_ = nullptr;
    void* swap_chain_ = nullptr;
    std::uint64_t present_count_ = 0;
};

} // namespace melee::native
