#pragma once

#include <cstdint>

namespace melee::native {

class NativeWindow final {
public:
    NativeWindow(std::uint32_t width = 1280, std::uint32_t height = 720,
                 bool visible = false);
    ~NativeWindow();
    NativeWindow(const NativeWindow&) = delete;
    NativeWindow& operator=(const NativeWindow&) = delete;

    bool valid() const noexcept { return handle_ != nullptr; }
    void show();
    bool pump_messages();
    void* handle() const noexcept { return handle_; }
    std::uint32_t width() const noexcept { return width_; }
    std::uint32_t height() const noexcept { return height_; }

private:
    void* handle_ = nullptr;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
};

} // namespace melee::native
