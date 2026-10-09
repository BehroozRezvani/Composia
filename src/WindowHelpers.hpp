#pragma once

#include <composia/Layout.hpp>
#include <composia/Native.hpp>
#include <climits>
#include <cmath>

// Win32 plumbing shared by Window, NativeControl, and Accessible.
namespace composia::detail {

// True when the window and every ancestor up to its top-level window are enabled.
inline bool effectively_enabled(HWND hwnd) noexcept {
    if (!hwnd) { return false; }
    for (auto current = hwnd; current; current = GetParent(current)) {
        if (!IsWindowEnabled(current)) { return false; }
        if (!(GetWindowLongPtrW(current, GWL_STYLE) & WS_CHILD)) { break; }
    }
    return true;
}

// Places a window at DIP bounds in its parent's client area, at the window's own DPI. Each edge
// is rounded on its own, so rectangles that share an edge in DIPs share it in pixels.
inline void place(HWND hwnd, layout::Rect bounds) {
    const auto dpi = GetDpiForWindow(hwnd);
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
    THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(hwnd, nullptr, x, y, static_cast<int>(width), static_cast<int>(height),
        SWP_NOZORDER | SWP_NOACTIVATE));
}

}
