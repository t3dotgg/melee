#pragma once

#include "native_render_geometry.h"

#include <array>
#include <cstdint>

namespace melee::native {

struct NativeSwapChainDesc {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    std::uint32_t buffer_count = 2;
    bool allow_tearing = true;
};

// Minimal Win32/DXGI presentation frontend. The window is hidden by default so
// callers can integrate it with their own host UI while retaining a real
// native swap chain and Present cadence.
class NativeWin32SwapChain final {
public:
    NativeWin32SwapChain() = default;
    ~NativeWin32SwapChain();
    NativeWin32SwapChain(const NativeWin32SwapChain&) = delete;
    NativeWin32SwapChain& operator=(const NativeWin32SwapChain&) = delete;

    bool initialize(const NativeSwapChainDesc& desc = {});
    void shutdown() noexcept;
    bool present() noexcept;
    // Prepare a validated, bounded vertex/index upload. Storage is retained and
    // reused across frames; draw ranges are copied and owned by the frontend.
    // The next clear_and_present submits pending copies and binds the buffers.
    // This does not issue draws: a material/shader pipeline is still required.
    bool prepare_geometry(const NativeRenderGeometry& geometry) noexcept;
    // Record and submit a native D3D12 render pass that clears the current
    // back buffer, waits for GPU completion, and presents it. This is the
    // first concrete command-recording boundary used by the native renderer.
    bool clear_and_present(float red, float green, float blue, float alpha = 1.0f,
                           bool capture = false) noexcept;
    std::span<const std::uint8_t> captured_pixels() const noexcept { return captured_pixels_; }
    void show() noexcept;
    void pump_messages() noexcept;
    bool available() const noexcept { return swap_chain_ != nullptr; }
    bool tearing_supported() const noexcept { return tearing_supported_; }
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }
    std::uint64_t presented_frames() const noexcept { return presented_frames_; }
    bool close_requested() const noexcept { return close_requested_; }
    const NativeGeometryUploadPlan& prepared_geometry() const noexcept { return geometry_plan_; }
    std::span<const NativeRenderDraw> prepared_draws() const noexcept { return geometry_draws_; }
    std::uint64_t geometry_uploads() const noexcept { return geometry_uploads_; }
    std::uint64_t draw_calls() const noexcept { return draw_calls_; }
    void* native_window() const noexcept { return window_; }

private:
    void* d3d12_module_ = nullptr;
    void* d3dcompiler_module_ = nullptr;
    void* dxgi_module_ = nullptr;
    void* device_ = nullptr;
    void* queue_ = nullptr;
    void* factory_ = nullptr;
    void* swap_chain_ = nullptr;
    void* command_allocator_ = nullptr;
    void* command_list_ = nullptr;
    void* rtv_heap_ = nullptr;
    void* fence_ = nullptr;
    void* fence_event_ = nullptr;
    void* root_signature_ = nullptr;
    void* pipeline_state_ = nullptr;
    void* geometry_upload_ = nullptr;
    void* geometry_buffer_ = nullptr;
    void* geometry_mapping_ = nullptr;
    NativeGeometryUploadPlan geometry_plan_{};
    std::vector<NativeRenderDraw> geometry_draws_;
    std::uint32_t geometry_capacity_ = 0;
    std::uint64_t geometry_uploads_ = 0;
    std::uint64_t draw_calls_ = 0;
    std::vector<std::uint8_t> captured_pixels_;
    bool geometry_pending_ = false;
    bool geometry_buffer_ready_ = false;
    bool submission_failed_ = false;
    std::array<void*, 8> back_buffers_{};
    std::uint32_t rtv_increment_ = 0;
    std::uint64_t fence_value_ = 0;
    void* window_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t buffer_count_ = 0;
    bool tearing_supported_ = false;
    bool com_initialized_ = false;
    bool class_registered_ = false;
    bool close_requested_ = false;
    std::uint64_t presented_frames_ = 0;
};

} // namespace melee::native
