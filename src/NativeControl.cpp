#include <composia/NativeControl.hpp>
#include <composia/Application.hpp>
#include <commctrl.h>
#include <cmath>
#include <limits>

namespace composia {
namespace {
constexpr wchar_t controlProperty[] = L"Composia.NativeControl";
constexpr UINT_PTR subclassId = 1;

HWND require_parent(const Window& parent) {
    const auto hwnd = parent.hwnd();
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), !hwnd);
    return hwnd;
}
}

NativeControl::NativeControl(Window& parent, std::wstring_view className, DWORD style, std::wstring_view text, DWORD extendedStyle)
    : parent_(parent) {
    const auto parentHwnd = require_parent(parent);
    const std::wstring ownedClass{className}, ownedText{text};
    const auto handle = CreateWindowExW(extendedStyle, ownedClass.c_str(), ownedText.c_str(), style | WS_CHILD, 0, 0, 0, 0,
        parentHwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    THROW_LAST_ERROR_IF_NULL(handle);
    hwnd_.reset(handle);
    THROW_IF_WIN32_BOOL_FALSE(SetPropW(handle, controlProperty, this));
    THROW_IF_WIN32_BOOL_FALSE(SetWindowSubclass(handle, subclass_proc, subclassId, reinterpret_cast<DWORD_PTR>(this)));
    apply_font();
}

NativeControl::~NativeControl() { detach(); }

void NativeControl::detach() noexcept {
    if (const auto handle = hwnd_.get()) {
        RemovePropW(handle, controlProperty);
        RemoveWindowSubclass(handle, subclass_proc, subclassId);
    }
}

NativeControl* NativeControl::from(HWND hwnd) noexcept {
    return hwnd ? static_cast<NativeControl*>(GetPropW(hwnd, controlProperty)) : nullptr;
}

LRESULT CALLBACK NativeControl::subclass_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR reference) noexcept {
    const auto self = reinterpret_cast<NativeControl*>(reference);
    switch (message) {
    case WM_DPICHANGED_AFTERPARENT:
        // The parent re-arranges bounds; the font has to follow the new monitor on its own.
        try { self->apply_font(); } catch (...) { LOG_CAUGHT_EXCEPTION(); }
        break;
    case WM_NCDESTROY:
        // The parent may be destroyed first; the wrapper must not keep a dead handle.
        RemovePropW(hwnd, controlProperty);
        RemoveWindowSubclass(hwnd, subclass_proc, subclassId);
        if (self->hwnd_.get() == hwnd) { (void)self->hwnd_.release(); }
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

HWND NativeControl::require_hwnd() const {
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), !hwnd_);
    return hwnd_.get();
}

void NativeControl::set_bounds(layout::Rect bounds) {
    const auto handle = require_hwnd();
    const auto dpi = GetDpiForWindow(handle);
    const auto scale = static_cast<double>(dpi ? dpi : 96) / 96.0;
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
    THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(handle, nullptr, x, y, static_cast<int>(width), static_cast<int>(height), SWP_NOZORDER | SWP_NOACTIVATE));
}

void NativeControl::show(bool visible) { ShowWindow(require_hwnd(), visible ? SW_SHOWNA : SW_HIDE); }
bool NativeControl::visible() const noexcept { return hwnd_ && IsWindowVisible(hwnd_.get()); }
void NativeControl::set_enabled(bool value) { EnableWindow(require_hwnd(), value); }

bool NativeControl::enabled() const noexcept {
    if (!hwnd_) { return false; }
    for (auto current = hwnd_.get(); current; current = GetParent(current)) {
        if (!IsWindowEnabled(current)) { return false; }
        if (!(GetWindowLongPtrW(current, GWL_STYLE) & WS_CHILD)) { break; }
    }
    return true;
}

void NativeControl::focus() { SetFocus(require_hwnd()); }
bool NativeControl::focused() const noexcept { return hwnd_ && GetFocus() == hwnd_.get(); }

std::wstring NativeControl::text() const {
    const auto handle = require_hwnd();
    const auto length = GetWindowTextLengthW(handle);
    std::wstring value(static_cast<std::size_t>(length), L'\0');
    if (length > 0) {
        const auto copied = GetWindowTextW(handle, value.data(), length + 1);
        value.resize(static_cast<std::size_t>(copied));
    }
    return value;
}

void NativeControl::set_text(std::wstring_view text) {
    const std::wstring owned{text};
    THROW_IF_WIN32_BOOL_FALSE(SetWindowTextW(require_hwnd(), owned.c_str()));
}

void NativeControl::set_font(std::wstring_view family, float pointSize, int weight) {
    THROW_HR_IF(E_INVALIDARG, family.empty() || !std::isfinite(pointSize) || pointSize <= 0 || pointSize > 1000);
    family_ = family;
    pointSize_ = pointSize;
    weight_ = weight;
    apply_font();
}

void NativeControl::apply_font() {
    const auto handle = require_hwnd();
    const auto dpi = GetDpiForWindow(handle);
    // Tenths of a point keep fractional sizes exact; integral sizes match -MulDiv(points, dpi, 72).
    const int height = -MulDiv(static_cast<int>(std::lround(pointSize_ * 10)), static_cast<int>(dpi ? dpi : 96), 720);
    wil::unique_hfont font{CreateFontW(height, 0, 0, 0, weight_, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family_.c_str())};
    THROW_LAST_ERROR_IF_NULL(font);
    SendMessageW(handle, WM_SETFONT, reinterpret_cast<WPARAM>(font.get()), TRUE);
    font_ = std::move(font);  // The previous font is released after the control has switched.
}

void NativeControl::set_colors(COLORREF text, COLORREF background) {
    const auto handle = require_hwnd();
    textColor_ = text;
    backgroundColor_ = background;
    background_.reset(CreateSolidBrush(background));
    THROW_LAST_ERROR_IF_NULL(background_);
    THROW_IF_WIN32_BOOL_FALSE(InvalidateRect(handle, nullptr, TRUE));
}

void NativeControl::clear_colors() {
    const auto handle = require_hwnd();
    textColor_.reset();
    backgroundColor_.reset();
    background_.reset();
    THROW_IF_WIN32_BOOL_FALSE(InvalidateRect(handle, nullptr, TRUE));
}

std::optional<LRESULT> NativeControl::control_color(HDC dc) {
    if (!backgroundColor_ || !background_) { return std::nullopt; }
    if (textColor_) { SetTextColor(dc, *textColor_); }
    SetBkColor(dc, *backgroundColor_);
    SetBkMode(dc, OPAQUE);
    return reinterpret_cast<LRESULT>(background_.get());
}

LRESULT NativeControl::send(UINT message, WPARAM wparam, LPARAM lparam) const {
    return SendMessageW(require_hwnd(), message, wparam, lparam);
}

void NativeControl::command(UINT code) { command_.emit(code); }
void NativeControl::notify(const NMHDR& header) { notify_.emit(header); }

}
