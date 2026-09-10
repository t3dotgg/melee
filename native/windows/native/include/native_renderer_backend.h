#pragma once

#include "native_render.h"

#include <cstdint>

namespace melee::native {

class NativeRendererBackend {
public:
    virtual ~NativeRendererBackend() = default;
    virtual bool initialize() = 0;
    virtual void submit(const RenderSnapshot& snapshot) = 0;
    virtual void present() = 0;
    virtual std::uint64_t presented_frames() const noexcept = 0;
};

// Headless backend used by tests and as the fallback for remote/unsupported
// systems. It still consumes ordered snapshots and presents at the caller's
// cadence, so timing behavior is testable without a window or GPU.
class NativeNullRenderer final : public NativeRendererBackend {
public:
    bool initialize() override { initialized_ = true; return true; }
    void submit(const RenderSnapshot& snapshot) override { if (initialized_) last_ = snapshot; }
    void present() override { if (initialized_ && !last_.objects.empty()) ++presented_; }
    std::uint64_t presented_frames() const noexcept override { return presented_; }
    const RenderSnapshot& last_snapshot() const noexcept { return last_; }

private:
    bool initialized_ = false;
    std::uint64_t presented_ = 0;
    RenderSnapshot last_;
};

// Capability probe for the concrete Windows graphics API. Device creation and
// swap-chain ownership remain in the frontend because they require a window
// handle; the native game only depends on this small backend contract.
class NativeD3D12Probe final {
public:
    NativeD3D12Probe();
    ~NativeD3D12Probe();
    NativeD3D12Probe(const NativeD3D12Probe&) = delete;
    NativeD3D12Probe& operator=(const NativeD3D12Probe&) = delete;
    bool available() const noexcept { return module_ != nullptr && create_device_ != nullptr; }

private:
    void* module_ = nullptr;
    void* create_device_ = nullptr;
};

} // namespace melee::native
