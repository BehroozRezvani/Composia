#pragma once

#include <composia/Native.hpp>
#include <composia/Layout.hpp>
#include <exception>
#include <optional>
#include <string_view>

namespace composia {

class Application;

// An owning HWND with exception-safe message dispatch. The base tracks pointer hover, pointer
// capture, keyboard focus, and the enabled state, so controls and application windows do not
// repeat that plumbing; raw messages stay available through on_message.
class Window {
public:
    Window(Application&, std::wstring_view title, int widthDip, int heightDip, HWND parent = nullptr);
    virtual ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void show(int command = SW_SHOWNORMAL);
    void invalidate();
    void set_bounds(layout::Rect);
    void rethrow_callback_error();
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_.get(); }
    [[nodiscard]] const wil::unique_hwnd& native_window() const noexcept { return hwnd_; }
    [[nodiscard]] UINT dpi() const noexcept;
    [[nodiscard]] SIZE client_pixels() const;
    [[nodiscard]] bool top_level() const noexcept { return topLevel_; }
    [[nodiscard]] Application& application() const noexcept { return application_; }

    // Keyboard focus. focused() is true while this HWND has the focus.
    void focus();
    [[nodiscard]] bool focused() const noexcept;
    // Enabled state. enabled() is false when this window or any ancestor is disabled.
    void set_enabled(bool);
    [[nodiscard]] bool enabled() const noexcept;
    // Pointer capture. Capture ends through release_pointer, WM_CANCELMODE, or another window
    // taking it; the latter two notify on_capture_lost.
    void capture_pointer();
    void release_pointer() noexcept;
    [[nodiscard]] bool pointer_captured() const noexcept { return captured_; }
    // True while the pointer is over the client area; on_hover reports changes.
    [[nodiscard]] bool hovered() const noexcept { return hovered_; }
    // DIP conversions at the window's DPI; the scale is 1 once the window is destroyed.
    [[nodiscard]] float scale() const noexcept;
    [[nodiscard]] layout::Point to_dips(POINT clientPixels) const noexcept;
    [[nodiscard]] layout::Point pointer_position(LPARAM) const noexcept;       // Client-relative mouse messages.
    [[nodiscard]] layout::Point pointer_position_from_screen(LPARAM) const;    // WM_MOUSEWHEEL and other screen-relative messages.
    [[nodiscard]] layout::Rect client_bounds() const;

protected:
    [[nodiscard]] HWND require_hwnd() const;
    virtual void on_graphics_recreated() {}
    virtual void on_resize() {}
    virtual void on_paint() {}
    virtual void on_hover(bool) {}
    virtual void on_focus(bool) {}
    virtual void on_capture_lost() {}
    virtual void on_enabled(bool) {}
    virtual std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) { return std::nullopt; }

private:
    friend class Application;
    static LRESULT CALLBACK window_proc(HWND, UINT, WPARAM, LPARAM) noexcept;
    LRESULT dispatch(HWND, UINT, WPARAM, LPARAM);
    void track(UINT, WPARAM, LPARAM);

    Application& application_;
    bool topLevel_{};
    wil::unique_hwnd hwnd_;
    std::exception_ptr callbackError_;
    bool hovered_{}, tracking_{}, captured_{};
};

}
