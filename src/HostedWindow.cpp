#include <composia/HostedWindow.hpp>
#include <composia/ElementWindow.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <commctrl.h>
#include <stdexcept>

namespace composia {
namespace {
constexpr UINT_PTR subclassId = 0x436F6D70; // 'Comp'

bool holds_focus(HWND window) noexcept {
    const auto focus = GetFocus();
    return focus && (focus == window || IsChild(window, focus));
}

int pixel(double dips, double scale) {
    return static_cast<int>(std::clamp(std::round(dips * scale), double(INT_MIN / 2), double(INT_MAX / 2)));
}

bool key_down(int key) { return (GetKeyState(key) & 0x8000) != 0; }
}

HostedWindow::HostedWindow(HWND window) : window_(window) {
    if (!window || !IsWindow(window) || !(GetWindowLongPtrW(window, GWL_STYLE) & WS_CHILD)) {
        throw std::invalid_argument("Expected a child window");
    }
    if (GetWindowThreadProcessId(window, nullptr) != GetCurrentThreadId()) {
        throw std::logic_error("A hosted window must belong to the calling thread");
    }
    THROW_IF_WIN32_BOOL_FALSE(SetWindowSubclass(window, subclass, subclassId, reinterpret_cast<DWORD_PTR>(this)));
    hostsWindow_ = true;
    set_focusable(true);
    hide();
}

HostedWindow::HostedWindow(Window& window) : HostedWindow(window.hwnd()) {}

HostedWindow::~HostedWindow() {
    if (window_) { RemoveWindowSubclass(window_, subclass, subclassId); }
}

LRESULT CALLBACK HostedWindow::subclass(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data) noexcept {
    auto* self = reinterpret_cast<HostedWindow*>(data);
    switch (message) {
    case WM_GETDLGCODE: {
        const auto code = DefSubclassProc(hwnd, message, wparam, lparam);
        // Tab the window leaves to the dialog manager continues in the tree's order instead.
        self->tabbing_ = wparam == VK_TAB && !(code & (DLGC_WANTTAB | DLGC_WANTALLKEYS | DLGC_WANTMESSAGE)) &&
            !key_down(VK_CONTROL) && !key_down(VK_MENU) && self->host_window();
        return self->tabbing_ ? code | DLGC_WANTTAB : code;
    }
    case WM_KEYDOWN:
        if (wparam == VK_TAB && self->tabbing_) {
            try { self->leave(key_down(VK_SHIFT)); }
            catch (...) { LOG_CAUGHT_EXCEPTION(); }
            return 0;
        }
        break;
    case WM_CHAR:
        // The character that follows a Tab the tree handled.
        if (wparam == L'\t' && std::exchange(self->tabbing_, false)) { return 0; }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, subclass, subclassId);
        self->window_ = nullptr;
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

void HostedWindow::place(ElementWindow& host) {
    if (!window_) { return; }
    if (GetParent(window_) != host.hwnd()) {
        throw std::logic_error("A hosted window must be a child of the ElementWindow that hosts its element");
    }
    const bool enable = enabled();
    if ((IsWindowEnabled(window_) != FALSE) != enable) {
        if (!enable && holds_focus(window_)) { SetFocus(host.hwnd()); }
        EnableWindow(window_, enable);
    }
    const auto box = viewport_bounds(), visible = viewport_bounds(true);
    if (visible.width <= 0 || visible.height <= 0) { hide(); return; }
    const double scale = host.scale();
    const RECT r{pixel(box.x, scale), pixel(box.y, scale),
                 pixel(double(box.x) + box.width, scale), pixel(double(box.y) + box.height, scale)};
    const RECT c{pixel(visible.x, scale) - r.left, pixel(visible.y, scale) - r.top,
                 pixel(double(visible.x) + visible.width, scale) - r.left,
                 pixel(double(visible.y) + visible.height, scale) - r.top};
    const bool clip = c.left > 0 || c.top > 0 || c.right < r.right - r.left || c.bottom < r.bottom - r.top;
    // The region goes first, so a window being revealed never shows its clipped part.
    if (clip && (!clipped_ || !EqualRect(&c, &clip_))) {
        wil::unique_hrgn region{CreateRectRgnIndirect(&c)};
        THROW_LAST_ERROR_IF_NULL(region.get());
        THROW_LAST_ERROR_IF(!SetWindowRgn(window_, region.get(), TRUE));
        region.release();
        clip_ = c;
    } else if (!clip && clipped_) {
        THROW_LAST_ERROR_IF(!SetWindowRgn(window_, nullptr, TRUE));
    }
    clipped_ = clip;
    const bool shown = (GetWindowLongPtrW(window_, GWL_STYLE) & WS_VISIBLE) != 0;
    if (!shown || !EqualRect(&r, &placed_)) {
        THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(window_, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
            SWP_NOZORDER | SWP_NOACTIVATE | (shown ? 0 : SWP_SHOWWINDOW)));
        placed_ = r;
    }
}

bool HostedWindow::take_focus() {
    if (!window_ || !IsWindowVisible(window_) || !IsWindowEnabled(window_)) { return false; }
    SetFocus(window_);
    return holds_focus(window_);
}

void HostedWindow::hide() noexcept {
    if (!window_ || !(GetWindowLongPtrW(window_, GWL_STYLE) & WS_VISIBLE)) { return; }
    if (holds_focus(window_)) { SetFocus(GetParent(window_)); }
    SetWindowPos(window_, nullptr, 0, 0, 0, 0, SWP_HIDEWINDOW | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void HostedWindow::on_detaching() noexcept { hide(); }

bool HostedWindow::leave(bool backwards) {
    auto* host = host_window();
    if (!host || !host->hwnd()) { return false; }
    auto* next = host->elements().next_focus_from(this, backwards, false);
    if (!next) {
        if (const auto outside = host->outside_tab_stop(backwards)) {
            SetFocus(outside);
            return GetFocus() == outside;
        }
        next = host->elements().next_focus_from(this, backwards, true);
    }
    return next && next != this && host->focus_element(*next);
}

}
