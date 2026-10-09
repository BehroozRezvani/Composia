#include <composia/Window.hpp>
#include <composia/Accessible.hpp>
#include <composia/Application.hpp>
#include <UIAutomation.h>
#include <string>
#include <cmath>
#include <limits>

namespace composia {
namespace {
constexpr wchar_t windowClass[] = L"Composia.Window";
}

Window::Window(Application& application, std::wstring_view title, int widthDip, int heightDip, HWND parent)
    : application_(application), topLevel_(parent == nullptr) {
    application_.verify_thread();
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

    const auto initialDpi = parent ? GetDpiForWindow(parent) : GetDpiForSystem();
    const DWORD style = parent ? WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | WS_VISIBLE
                               : WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    const DWORD extendedStyle = parent ? 0 : WS_EX_CONTROLPARENT;
    RECT bounds{0, 0, MulDiv(widthDip, initialDpi, 96), MulDiv(heightDip, initialDpi, 96)};
    THROW_IF_WIN32_BOOL_FALSE(AdjustWindowRectExForDpi(&bounds, style, FALSE, extendedStyle, initialDpi));
    const std::wstring ownedTitle{title};
    auto handle = CreateWindowExW(extendedStyle, windowClass, ownedTitle.c_str(), style,
        parent ? 0 : CW_USEDEFAULT, parent ? 0 : CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
        parent, nullptr, instance, this);
    hwnd_.reset(handle);
    rethrow_callback_error();
    THROW_LAST_ERROR_IF_NULL(handle);
    const auto actualDpi = GetDpiForWindow(handle);
    if (actualDpi != initialDpi && actualDpi != 0) {
        bounds = {0, 0, MulDiv(widthDip, actualDpi, 96), MulDiv(heightDip, actualDpi, 96)};
        THROW_IF_WIN32_BOOL_FALSE(AdjustWindowRectExForDpi(&bounds, style, FALSE, extendedStyle, actualDpi));
        THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(handle, nullptr, 0, 0, bounds.right - bounds.left, bounds.bottom - bounds.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        rethrow_callback_error();
    }
    application_.attach(*this);
}

Window::~Window() {
    application_.detach(*this);
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_.get(), GWLP_USERDATA, 0);
    }
}

HWND Window::require_hwnd() const {
    application_.verify_thread();
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), !hwnd_);
    return hwnd();
}

void Window::show(int command) { ShowWindow(require_hwnd(), command); }
void Window::invalidate() { THROW_IF_WIN32_BOOL_FALSE(InvalidateRect(require_hwnd(), nullptr, FALSE)); }
void Window::set_bounds(layout::Rect bounds) {
    const auto handle = require_hwnd();
    const auto scale = static_cast<double>(dpi()) / 96.0;
    const auto pixel = [scale](double value) {
        const auto rounded = std::round(value * scale);
        THROW_HR_IF(E_INVALIDARG, !std::isfinite(rounded) || rounded < INT_MIN || rounded > INT_MAX);
        return static_cast<int>(rounded);
    };
    THROW_HR_IF(E_INVALIDARG, bounds.width < 0 || bounds.height < 0);
    const int x = pixel(bounds.x), y = pixel(bounds.y);
    const auto width = static_cast<long long>(pixel(static_cast<double>(bounds.x) + bounds.width)) - x;
    const auto height = static_cast<long long>(pixel(static_cast<double>(bounds.y) + bounds.height)) - y;
    THROW_HR_IF(E_INVALIDARG, width > INT_MAX || height > INT_MAX);
    THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(handle, nullptr, x, y, static_cast<int>(width), static_cast<int>(height),
        SWP_NOZORDER | SWP_NOACTIVATE));
}
UINT Window::dpi() const noexcept { return GetDpiForWindow(hwnd()); }

SIZE Window::client_pixels() const {
    RECT rect{};
    THROW_IF_WIN32_BOOL_FALSE(GetClientRect(require_hwnd(), &rect));
    return {rect.right - rect.left, rect.bottom - rect.top};
}

void Window::focus() { SetFocus(require_hwnd()); }
bool Window::focused() const noexcept { return hwnd_ && GetFocus() == hwnd_.get(); }
void Window::set_enabled(bool value) { EnableWindow(require_hwnd(), value); }

bool Window::enabled() const noexcept {
    if (!hwnd_) { return false; }
    for (auto current = hwnd_.get(); current; current = GetParent(current)) {
        if (!IsWindowEnabled(current)) { return false; }
        if (!(GetWindowLongPtrW(current, GWL_STYLE) & WS_CHILD)) { break; }
    }
    return true;
}

void Window::capture_pointer() {
    SetCapture(require_hwnd());
    captured_ = true;
}

void Window::release_pointer() noexcept {
    if (!captured_) { return; }
    captured_ = false;
    if (hwnd_ && GetCapture() == hwnd_.get()) { ReleaseCapture(); }
}

float Window::scale() const noexcept {
    const auto value = dpi();
    return value ? static_cast<float>(value) / 96.0f : 1.0f;
}

layout::Point Window::to_dips(POINT pixels) const noexcept {
    const auto factor = scale();
    return {static_cast<float>(pixels.x) / factor, static_cast<float>(pixels.y) / factor};
}

layout::Point Window::pointer_position(LPARAM lparam) const noexcept {
    return to_dips({static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))});
}

layout::Point Window::pointer_position_from_screen(LPARAM lparam) const {
    POINT point{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
    THROW_IF_WIN32_BOOL_FALSE(ScreenToClient(require_hwnd(), &point));
    return to_dips(point);
}

layout::Rect Window::client_bounds() const {
    const auto pixels = client_pixels();
    const auto factor = scale();
    return {0, 0, static_cast<float>(pixels.cx) / factor, static_cast<float>(pixels.cy) / factor};
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
        self->application_.report_error(self->callbackError_);
        return message == WM_NCCREATE ? FALSE : 0;
    }
}

// Keeps the hover, capture, focus, and enabled state current before on_message sees the message,
// so overrides can rely on it whether or not they handle the message themselves.
void Window::track(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_MOUSEMOVE:
        if (!tracking_ && hwnd_) {
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd_.get(), 0};
            if (TrackMouseEvent(&tracking)) { tracking_ = true; }
        }
        if (!hovered_) { hovered_ = true; on_hover(true); }
        break;
    case WM_MOUSELEAVE:
        tracking_ = false;
        if (hovered_) { hovered_ = false; on_hover(false); }
        break;
    case WM_SETFOCUS:
        if (accessible_) { accessible_->focus_changed(true); }
        on_focus(true);
        break;
    case WM_KILLFOCUS:
        if (accessible_) { accessible_->focus_changed(false); }
        on_focus(false);
        break;
    case WM_CAPTURECHANGED:
        if (captured_ && reinterpret_cast<HWND>(lparam) != hwnd_.get()) {
            captured_ = false;
            on_capture_lost();
        }
        break;
    case WM_CANCELMODE:
        if (captured_) {
            captured_ = false;
            ReleaseCapture();
            on_capture_lost();
        }
        break;
    case WM_ENABLE:
        if (accessible_) { accessible_->enabled_changed(wparam != 0); }
        on_enabled(wparam != 0);
        break;
    case WM_DESTROY:
        hovered_ = tracking_ = false;
        if (captured_) {
            captured_ = false;
            ReleaseCapture();
        }
        if (accessible_) { accessible_->disconnect(); }
        break;
    }
}

LRESULT Window::dispatch(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    track(message, wparam, lparam);
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
    case WM_DPICHANGED_AFTERPARENT:
        on_resize();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(handle, &paint);
        const auto endPaint = wil::scope_exit([&] { EndPaint(handle, &paint); });
        on_paint();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_GETOBJECT:
        if (accessible_ && static_cast<LONG>(lparam) == UiaRootObjectId) {
            return accessible_->root_provider(wparam, lparam);
        }
        return DefWindowProcW(handle, message, wparam, lparam);
    case WM_DESTROY:
        return 0;
    default:
        return DefWindowProcW(handle, message, wparam, lparam);
    }
}

}
