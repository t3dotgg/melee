#include "native_window.h"

#ifdef _WIN32
#include <windows.h>

namespace {
constexpr wchar_t kClassName[] = L"MeleeNativeWindow";
LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CLOSE) { DestroyWindow(window); return 0; }
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wparam, lparam);
}
}
#endif

namespace melee::native {

NativeWindow::NativeWindow(std::uint32_t width, std::uint32_t height, bool visible)
    : width_(width), height_(height)
{
#ifdef _WIN32
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW klass{};
    klass.hInstance = instance;
    klass.lpfnWndProc = window_proc;
    klass.lpszClassName = kClassName;
    klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    RegisterClassW(&klass);
    const DWORD style = visible ? WS_OVERLAPPEDWINDOW : WS_POPUP;
    HWND window = CreateWindowExW(0, kClassName, L"Melee (native Windows)", style,
                                 CW_USEDEFAULT, CW_USEDEFAULT, static_cast<int>(width),
                                 static_cast<int>(height), nullptr, nullptr, instance, nullptr);
    handle_ = window;
    if (window != nullptr && visible) ShowWindow(window, SW_SHOW);
#else
    (void)visible;
#endif
}

NativeWindow::~NativeWindow()
{
#ifdef _WIN32
    if (handle_ != nullptr) DestroyWindow(static_cast<HWND>(handle_));
#endif
    handle_ = nullptr;
}

void NativeWindow::show()
{
#ifdef _WIN32
    if (handle_ != nullptr) ShowWindow(static_cast<HWND>(handle_), SW_SHOW);
#endif
}

bool NativeWindow::pump_messages()
{
#ifdef _WIN32
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) { handle_ = nullptr; return false; }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
#endif
    return handle_ != nullptr;
}

} // namespace melee::native
