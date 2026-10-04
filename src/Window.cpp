#include <composia/Window.hpp>
#include <string>

namespace composia {
namespace {
constexpr wchar_t windowClass[] = L"Composia.Window";
}

Window::Window(std::wstring_view title, int widthDip, int heightDip) {
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = windowClass;
    if (!RegisterClassExW(&wc)) {
        THROW_LAST_ERROR_IF(GetLastError() != ERROR_CLASS_ALREADY_EXISTS);
    }

    const auto initialDpi = GetDpiForSystem();
    RECT bounds{0, 0, MulDiv(widthDip, initialDpi, 96), MulDiv(heightDip, initialDpi, 96)};
    THROW_IF_WIN32_BOOL_FALSE(AdjustWindowRectExForDpi(&bounds, WS_OVERLAPPEDWINDOW, FALSE, 0, initialDpi));
    const std::wstring ownedTitle{title};
    auto handle = CreateWindowExW(0, windowClass, ownedTitle.c_str(), WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        nullptr, nullptr, instance, this);
    hwnd_.reset(handle);
    rethrow_callback_error();
    THROW_LAST_ERROR_IF_NULL(handle);
}

Window::~Window() {
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_.get(), GWLP_USERDATA, 0);
    }
}

void Window::show(int command) { ShowWindow(hwnd(), command); }
void Window::invalidate() { THROW_IF_WIN32_BOOL_FALSE(InvalidateRect(hwnd(), nullptr, FALSE)); }
UINT Window::dpi() const noexcept { return GetDpiForWindow(hwnd()); }

SIZE Window::client_pixels() const {
    RECT rect{};
    THROW_IF_WIN32_BOOL_FALSE(GetClientRect(hwnd(), &rect));
    return {rect.right - rect.left, rect.bottom - rect.top};
}

void Window::rethrow_callback_error() {
    if (callbackError_) {
        std::rethrow_exception(callbackError_);
    }
}

LRESULT CALLBACK Window::window_proc(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    auto self = reinterpret_cast<Window*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Window*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
        SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) {
        return DefWindowProcW(handle, message, wparam, lparam);
    }
    if (message == WM_NCDESTROY) {
        SetWindowLongPtrW(handle, GWLP_USERDATA, 0);
        // An external DestroyWindow must not leave an owning wrapper with a stale handle.
        if (self->hwnd_.get() == handle) {
            (void)self->hwnd_.release();
        }
        return DefWindowProcW(handle, message, wparam, lparam);
    }
    try {
        return self->dispatch(handle, message, wparam, lparam);
    } catch (...) {
        if (!self->callbackError_) {
            self->callbackError_ = std::current_exception();
        }
        PostQuitMessage(1);
        return message == WM_NCCREATE ? FALSE : 0;
    }
}

LRESULT Window::dispatch(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    if (auto result = on_message(message, wparam, lparam)) {
        return *result;
    }
    switch (message) {
    case WM_SIZE:
        if (hwnd_ && wparam != SIZE_MINIMIZED) {
            on_resize();
        }
        return 0;
    case WM_DPICHANGED: {
        const auto rect = reinterpret_cast<RECT*>(lparam);
        THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(handle, nullptr, rect->left, rect->top,
            rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE));
        on_resize();
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(handle, &paint);
        const auto endPaint = wil::scope_exit([&] { EndPaint(handle, &paint); });
        on_paint();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(handle, message, wparam, lparam);
    }
}

}
