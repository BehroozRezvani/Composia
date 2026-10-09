#include <composia/NativeControl.hpp>
#include "WindowHelpers.hpp"
#include <commctrl.h>
#include <uxtheme.h>
#include <cmath>
#include <cwchar>
#include <iterator>

namespace composia {
namespace {
constexpr UINT_PTR subclassId = 1;

bool has_class(HWND hwnd, const wchar_t* name) noexcept {
    wchar_t actual[32]{};
    return GetClassNameW(hwnd, actual, static_cast<int>(std::size(actual))) && _wcsicmp(actual, name) == 0;
}
}

NativeControl::NativeControl(Window& parent, std::wstring_view className, DWORD style, std::wstring_view text, DWORD extendedStyle)
    : parent_(parent) {
    const auto parentHwnd = parent.hwnd();
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), !parentHwnd);
    const std::wstring ownedClass{className}, ownedText{text};
    const auto handle = CreateWindowExW(extendedStyle, ownedClass.c_str(), ownedText.c_str(), style | WS_CHILD, 0, 0, 0, 0,
        parentHwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    THROW_LAST_ERROR_IF_NULL(handle);
    hwnd_.reset(handle);
    auto undo = wil::scope_exit([&] { detach(); });
    Window::set_notification_handler(handle, this);
    // A drop-down list is a separate window, but it asks for colors through the combo box.
    COMBOBOXINFO combo{};
    combo.cbSize = sizeof(combo);
    if (has_class(handle, L"ComboBox") && GetComboBoxInfo(handle, &combo) && combo.hwndList && !IsChild(handle, combo.hwndList)) {
        Window::set_notification_handler(combo.hwndList, this);
        list_ = combo.hwndList;
    }
    THROW_IF_WIN32_BOOL_FALSE(SetWindowSubclass(handle, subclass_proc, subclassId, reinterpret_cast<DWORD_PTR>(this)));
    apply_font();
    undo.release();
}

NativeControl::~NativeControl() {
    detach();
    // The control goes before the font and brush it uses, as WM_SETFONT requires.
    hwnd_.reset();
}

void NativeControl::detach() noexcept {
    if (list_) {
        if (IsWindow(list_)) { Window::set_notification_handler(list_, nullptr); }
        list_ = nullptr;
    }
    if (const auto handle = hwnd_.get()) {
        Window::set_notification_handler(handle, nullptr);
        RemoveWindowSubclass(handle, subclass_proc, subclassId);
    }
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
        self->detach();
        if (self->hwnd_.get() == hwnd) { (void)self->hwnd_.release(); }
        break;
    }
    return DefSubclassProc(hwnd, message, wparam, lparam);
}

std::optional<LRESULT> NativeControl::notification(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
        command_.emit(HIWORD(wparam));
        return 0;
    case WM_NOTIFY:
        notify_.emit(*reinterpret_cast<const NMHDR*>(lparam));
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
        return control_color(reinterpret_cast<HDC>(wparam));
    default:
        return std::nullopt;
    }
}

HWND NativeControl::require_hwnd() const {
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), !hwnd_);
    return hwnd_.get();
}

void NativeControl::set_bounds(layout::Rect bounds) { detail::place(require_hwnd(), bounds); }
void NativeControl::show(bool visible) { ShowWindow(require_hwnd(), visible ? SW_SHOWNA : SW_HIDE); }
bool NativeControl::visible() const noexcept { return hwnd_ && IsWindowVisible(hwnd_.get()); }
void NativeControl::set_enabled(bool value) { EnableWindow(require_hwnd(), value); }
bool NativeControl::enabled() const noexcept { return detail::effectively_enabled(hwnd_.get()); }
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

// Check boxes, radio buttons, and group boxes draw their text with the theme's color while visual
// styles are on, whatever WM_CTLCOLORSTATIC selects into the device context.
bool NativeControl::theme_colors_text() const noexcept {
    const auto handle = hwnd_.get();
    if (!handle || !has_class(handle, L"Button")) { return false; }
    const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
    if (style & BS_PUSHLIKE) { return false; }
    switch (style & BS_TYPEMASK) {
    case BS_CHECKBOX:
    case BS_AUTOCHECKBOX:
    case BS_3STATE:
    case BS_AUTO3STATE:
    case BS_RADIOBUTTON:
    case BS_AUTORADIOBUTTON:
    case BS_GROUPBOX:
        return true;
    default:
        return false;
    }
}

void NativeControl::set_colors(COLORREF text, COLORREF background) {
    const auto handle = require_hwnd();
    wil::unique_hbrush brush{CreateSolidBrush(background)};
    THROW_LAST_ERROR_IF_NULL(brush);
    if (!unthemed_ && theme_colors_text()) {
        THROW_IF_FAILED(SetWindowTheme(handle, L"", L""));
        unthemed_ = true;
    }
    textColor_ = text;
    backgroundColor_ = background;
    background_ = std::move(brush);
    THROW_IF_WIN32_BOOL_FALSE(InvalidateRect(handle, nullptr, TRUE));
}

void NativeControl::clear_colors() {
    const auto handle = require_hwnd();
    if (unthemed_) {
        THROW_IF_FAILED(SetWindowTheme(handle, nullptr, nullptr));
        unthemed_ = false;
    }
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

}
