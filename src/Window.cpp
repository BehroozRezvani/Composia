#include <composia/Window.hpp>
#include <composia/Application.hpp>
#include "WindowHelpers.hpp"
#include <UIAutomation.h>
#include <dwmapi.h>
#include <algorithm>
#include <cmath>
#include <climits>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

namespace composia {
namespace {
constexpr wchar_t windowClass[] = L"Composia.Window";
constexpr wchar_t handlerProperty[] = L"Composia.NotificationHandler";

// The Composia window behind an HWND of this thread, or null.
Window* window_from(HWND hwnd) noexcept {
    if (!hwnd || GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId()) { return nullptr; }
    wchar_t name[std::size(windowClass)]{};
    if (!GetClassNameW(hwnd, name, static_cast<int>(std::size(name))) || std::wcscmp(name, windowClass) != 0) { return nullptr; }
    return reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

BOOL CALLBACK collect_window(HWND hwnd, LPARAM context) noexcept {
    try {
        reinterpret_cast<std::vector<HWND>*>(context)->push_back(hwnd);
        return TRUE;
    } catch (...) { return FALSE; }
}

// The control a notification names: lparam for WM_COMMAND, WM_CTLCOLOR*, and the scroll
// messages, the header for WM_NOTIFY, and the item for WM_DRAWITEM. Menus name no control.
HWND notification_source(UINT message, LPARAM lparam) noexcept {
    switch (message) {
    case WM_COMMAND:
    case WM_HSCROLL:
    case WM_VSCROLL:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORSCROLLBAR:
        return reinterpret_cast<HWND>(lparam);
    case WM_NOTIFY:
        return lparam ? reinterpret_cast<const NMHDR*>(lparam)->hwndFrom : nullptr;
    case WM_DRAWITEM: {
        const auto item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        return item && item->CtlType != ODT_MENU ? item->hwndItem : nullptr;
    }
    default:
        return nullptr;
    }
}

// Composite controls send notifications from inner windows, such as a combo box's edit field,
// so the search continues through the ancestors up to the parent window.
NotificationHandler* notification_handler(HWND parent, HWND control) noexcept {
    const auto thread = GetCurrentThreadId();
    for (auto current = control; current && current != parent; current = GetParent(current)) {
        if (GetWindowThreadProcessId(current, nullptr) != thread) { return nullptr; }
        if (const auto handler = static_cast<NotificationHandler*>(GetPropW(current, handlerProperty))) { return handler; }
    }
    return nullptr;
}
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
    reportedEnabled_ = enabled();
    application_.attach(*this);
}

Window::~Window() {
    const bool served = automationProvider_ != nullptr;
    // Subscribers such as an Accessible may outlive this object; they must let go of it first.
    try { notify_destroy(); } catch (...) { LOG_CAUGHT_EXCEPTION(); }
    application_.detach(*this);
    if (hwnd_) {
        // Lets UI Automation release what it holds for this window.
        if (served) { (void)UiaReturnRawElementProvider(hwnd_.get(), 0, 0, nullptr); }
        SetWindowLongPtrW(hwnd_.get(), GWLP_USERDATA, 0);
    }
    set_automation_provider(nullptr);
}

HWND Window::require_hwnd() const {
    application_.verify_thread();
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), !hwnd_);
    return hwnd();
}

void Window::show(int command) { ShowWindow(require_hwnd(), command); }
void Window::invalidate() { THROW_IF_WIN32_BOOL_FALSE(InvalidateRect(require_hwnd(), nullptr, FALSE)); }

void Window::invalidate(layout::Rect bounds) {
    const auto handle = require_hwnd();
    THROW_HR_IF(E_INVALIDARG, !std::isfinite(bounds.x) || !std::isfinite(bounds.y) || !std::isfinite(bounds.width) ||
        !std::isfinite(bounds.height) || bounds.width < 0 || bounds.height < 0);
    if (bounds.width == 0 || bounds.height == 0) { return; }
    const auto factor = static_cast<double>(dpi()) / 96.0;
    const auto pixel = [](double value) {
        return static_cast<LONG>(std::clamp(value, static_cast<double>(INT_MIN), static_cast<double>(INT_MAX)));
    };
    const RECT area{pixel(std::floor(bounds.x * factor)), pixel(std::floor(bounds.y * factor)),
        pixel(std::ceil((static_cast<double>(bounds.x) + bounds.width) * factor)),
        pixel(std::ceil((static_cast<double>(bounds.y) + bounds.height) * factor))};
    THROW_IF_WIN32_BOOL_FALSE(InvalidateRect(handle, &area, FALSE));
}

void Window::set_bounds(layout::Rect bounds) { detail::place(require_hwnd(), bounds); }
UINT Window::dpi() const noexcept { return GetDpiForWindow(hwnd()); }

SIZE Window::client_pixels() const {
    RECT rect{};
    THROW_IF_WIN32_BOOL_FALSE(GetClientRect(require_hwnd(), &rect));
    return {rect.right - rect.left, rect.bottom - rect.top};
}

void Window::focus() { SetFocus(require_hwnd()); }
bool Window::focused() const noexcept { return hwnd_ && GetFocus() == hwnd_.get(); }
void Window::set_enabled(bool value) { EnableWindow(require_hwnd(), value); }
bool Window::enabled() const noexcept { return detail::effectively_enabled(hwnd_.get()); }

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

void Window::set_automation_provider(IRawElementProviderSimple* provider) noexcept {
    if (provider) { provider->AddRef(); }
    if (automationProvider_) { automationProvider_->Release(); }
    automationProvider_ = provider;
}

void Window::set_dark_title_bar(bool dark) {
    const auto handle = require_hwnd();
    THROW_HR_IF(E_INVALIDARG, !topLevel_);
    const BOOL value = dark ? TRUE : FALSE;
    // Windows 10 before version 2004 knows DWMWA_USE_IMMERSIVE_DARK_MODE as 19.
    if (FAILED(DwmSetWindowAttribute(handle, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value)))) {
        THROW_IF_FAILED(DwmSetWindowAttribute(handle, 19, &value, sizeof(value)));
    }
}

void Window::set_notification_handler(HWND control, NotificationHandler* handler) {
    if (!handler) {
        if (control) { RemovePropW(control, handlerProperty); }
        return;
    }
    THROW_HR_IF(E_INVALIDARG, !control || GetWindowThreadProcessId(control, nullptr) != GetCurrentThreadId());
    THROW_IF_WIN32_BOOL_FALSE(SetPropW(control, handlerProperty, handler));
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

void Window::update_hover(bool inside) {
    if (hovered_ == inside) { return; }
    hovered_ = inside;
    on_hover(inside);
    hoverChanged_.emit(inside);
}

void Window::notify_capture_lost() {
    on_capture_lost();
    captureLost_.emit();
}

void Window::refresh_enabled() {
    const bool now = enabled();
    if (now == reportedEnabled_) { return; }
    reportedEnabled_ = now;
    on_enabled(now);
    enabledChanged_.emit(now);
}

// WM_ENABLE reaches only the window whose own state changed, but enabled() also depends on the
// ancestors, so every Composia window below it is refreshed too.
void Window::propagate_enabled() {
    std::vector<HWND> descendants;
    EnumChildWindows(hwnd_.get(), collect_window, reinterpret_cast<LPARAM>(&descendants));
    std::exception_ptr firstError;
    const auto refresh = [&](Window& window) {
        try { window.refresh_enabled(); }
        catch (...) { if (!firstError) { firstError = std::current_exception(); } }
    };
    refresh(*this);
    for (const auto child : descendants) {
        // A callback may have destroyed later windows; window_from rejects those handles.
        if (const auto window = window_from(child)) { refresh(*window); }
    }
    if (firstError) { std::rethrow_exception(firstError); }
}

void Window::remember_focus(HWND focus) noexcept {
    if (focus && hwnd_ && (focus == hwnd_.get() || IsChild(hwnd_.get(), focus))) { lastFocus_ = focus; }
}

// Default activation focuses the top-level window itself. Instead, the window inside it that had
// the focus before deactivation gets it back, as the dialog manager does.
bool Window::restore_focus() {
    const auto target = lastFocus_;
    if (!target || target == hwnd_.get() || !IsWindow(target) || !IsChild(hwnd_.get(), target) ||
        !IsWindowVisible(target) || !detail::effectively_enabled(target)) {
        return false;
    }
    SetFocus(target);
    return GetFocus() == target;
}

void Window::notify_destroy() {
    if (destroyNotified_ || !hwnd_) { return; }
    destroyNotified_ = true;
    destroying_.emit();
}

// Keeps the hover, capture, focus, and enabled state current before on_message sees the message,
// so overrides can rely on it whether or not they handle the message themselves.
void Window::track(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN: case WM_XBUTTONUP: case WM_XBUTTONDBLCLK: {
        // While this window has captured the pointer, these messages also arrive from outside the
        // client area, and Windows reports no leave until the capture ends.
        RECT client{};
        const POINT point{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
        const bool inside = hwnd_ && GetClientRect(hwnd_.get(), &client) && PtInRect(&client, point);
        if (inside && !tracking_) {
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd_.get(), 0};
            if (TrackMouseEvent(&tracking)) { tracking_ = true; }
        }
        update_hover(inside);
        break;
    }
    case WM_MOUSELEAVE:
        tracking_ = false;
        update_hover(false);
        break;
    case WM_SETFOCUS:
        on_focus(true);
        focusChanged_.emit(true);
        break;
    case WM_KILLFOCUS:
        on_focus(false);
        focusChanged_.emit(false);
        break;
    case WM_ACTIVATE:
        // Minimizing clears the focus before deactivating, so the message loop also records it.
        if (topLevel_ && LOWORD(wparam) == WA_INACTIVE) { remember_focus(GetFocus()); }
        break;
    case WM_CAPTURECHANGED:
        if (captured_ && reinterpret_cast<HWND>(lparam) != hwnd_.get()) {
            captured_ = false;
            notify_capture_lost();
        }
        break;
    case WM_CANCELMODE:
        if (captured_) {
            captured_ = false;  // The WM_CAPTURECHANGED that ReleaseCapture sends is not a second loss.
            ReleaseCapture();
            notify_capture_lost();
        }
        break;
    case WM_ENABLE:
        propagate_enabled();
        break;
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_THEMECHANGED:
    case WM_DWMCOLORIZATIONCOLORCHANGED:
        // Windows announces appearance changes to top-level windows. The application reads the
        // settings again and tells every window only if they changed.
        if (topLevel_) { application_.refresh_appearance(); }
        break;
    case WM_DESTROY: {
        hovered_ = tracking_ = false;
        lastFocus_ = nullptr;
        if (captured_) {
            captured_ = false;
            ReleaseCapture();
        }
        const bool served = automationProvider_ != nullptr;
        const auto releaseProvider = wil::scope_exit([&] {
            if (served) { (void)UiaReturnRawElementProvider(hwnd_.get(), 0, 0, nullptr); }
            set_automation_provider(nullptr);
        });
        notify_destroy();
        break;
    }
    }
}

LRESULT Window::dispatch(HWND handle, UINT message, WPARAM wparam, LPARAM lparam) {
    track(message, wparam, lparam);
    if (auto result = on_message(message, wparam, lparam)) {
        return *result;
    }
    // Hosted controls handle their own notifications, color requests, and owner drawing.
    if (const auto source = notification_source(message, lparam)) {
        if (const auto handler = notification_handler(handle, source)) {
            if (const auto result = handler->notification(message, wparam, lparam)) { return *result; }
        }
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
    case WM_ACTIVATE:
        if (topLevel_ && LOWORD(wparam) != WA_INACTIVE && !HIWORD(wparam) && restore_focus()) { return 0; }
        return DefWindowProcW(handle, message, wparam, lparam);
    case WM_NEXTDLGCTL: {
        // Multiline edit controls hand Tab to their parent this way; only dialogs answer it by default.
        const auto target = LOWORD(lparam) ? reinterpret_cast<HWND>(wparam)
            : GetNextDlgTabItem(GetAncestor(handle, GA_ROOT), GetFocus(), wparam != 0);
        if (target) {
            SetFocus(target);
            if (SendMessageW(target, WM_GETDLGCODE, 0, 0) & DLGC_HASSETSEL) { SendMessageW(target, EM_SETSEL, 0, -1); }
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(handle, &paint);
        const auto endPaint = wil::scope_exit([&] {
            paintRect_.reset();
            EndPaint(handle, &paint);
        });
        if (!IsRectEmpty(&paint.rcPaint)) { paintRect_ = paint.rcPaint; }
        on_paint();
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_GETOBJECT:
        if (automationProvider_ && static_cast<LONG>(lparam) == UiaRootObjectId) {
            return UiaReturnRawElementProvider(handle, wparam, lparam, automationProvider_);
        }
        return DefWindowProcW(handle, message, wparam, lparam);
    case WM_DESTROY:
        return 0;
    default:
        return DefWindowProcW(handle, message, wparam, lparam);
    }
}

}
