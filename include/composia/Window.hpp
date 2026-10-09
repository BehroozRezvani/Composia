#pragma once

#include <composia/Native.hpp>
#include <composia/Layout.hpp>
#include <composia/Signal.hpp>
#include <exception>
#include <functional>
#include <optional>
#include <string_view>

struct IRawElementProviderSimple;

namespace composia {

class Application;

// Receives the notifications a child control sends to its parent Window: WM_COMMAND, WM_NOTIFY,
// WM_CTLCOLOR*, WM_DRAWITEM, WM_HSCROLL, and WM_VSCROLL. Register one for a control's HWND with
// Window::set_notification_handler. The parent asks the handler of the control named by the
// message, or of its nearest ancestor that has one, after on_message and before its default
// processing. NativeControl is built on this.
class NotificationHandler {
public:
    // The message result, or nullopt to leave the message to the parent's default processing.
    virtual std::optional<LRESULT> notification(UINT message, WPARAM wparam, LPARAM lparam) = 0;

protected:
    ~NotificationHandler() = default;
};

// An owning HWND with exception-safe message dispatch. The base tracks pointer hover, pointer
// capture, keyboard focus, and the enabled state, so controls and application windows do not
// repeat that plumbing; raw messages stay available through on_message. Derived classes receive
// state changes through virtual hooks, and any other code can subscribe to them as signals.
class Window {
public:
    Window(Application&, std::wstring_view title, int widthDip, int heightDip, HWND parent = nullptr);
    virtual ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void show(int command = SW_SHOWNORMAL);
    // Schedules a repaint of the whole client area, or of a DIP rectangle rounded out to whole
    // pixels. Requests coalesce into one WM_PAINT; on_paint reads the area from paint_rect().
    void invalidate();
    void invalidate(layout::Rect);
    void set_bounds(layout::Rect);
    void rethrow_callback_error();
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_.get(); }
    [[nodiscard]] UINT dpi() const noexcept;
    [[nodiscard]] SIZE client_pixels() const;
    [[nodiscard]] bool top_level() const noexcept { return topLevel_; }
    [[nodiscard]] Application& application() const noexcept { return application_; }
    // The client pixels being repainted while on_paint runs; nullopt at any other time.
    [[nodiscard]] std::optional<RECT> paint_rect() const noexcept { return paintRect_; }

    // Keyboard focus. focused() is true while this HWND has the focus. When a top-level window is
    // activated again, it returns the focus to the window inside it that last had it.
    void focus();
    [[nodiscard]] bool focused() const noexcept;
    // Enabled state. enabled() is false when this window or any ancestor is disabled, and
    // on_enabled reports changes from either source.
    void set_enabled(bool);
    [[nodiscard]] bool enabled() const noexcept;
    // Pointer capture. Capture ends through release_pointer, WM_CANCELMODE, or another window
    // taking it; the latter two notify on_capture_lost.
    void capture_pointer();
    void release_pointer() noexcept;
    [[nodiscard]] bool pointer_captured() const noexcept { return captured_; }
    // True while the pointer is over the client area, including while this window has captured
    // it; on_hover reports changes.
    [[nodiscard]] bool hovered() const noexcept { return hovered_; }
    // DIP conversions at the window's DPI; the scale is 1 once the window is destroyed.
    [[nodiscard]] float scale() const noexcept;
    [[nodiscard]] layout::Point to_dips(POINT clientPixels) const noexcept;
    [[nodiscard]] layout::Point pointer_position(LPARAM) const noexcept;       // Client-relative mouse messages.
    [[nodiscard]] layout::Point pointer_position_from_screen(LPARAM) const;    // WM_MOUSEWHEEL and other screen-relative messages.
    [[nodiscard]] layout::Rect client_bounds() const;

    // The state changes behind the virtual hooks, for code that does not derive from the window,
    // such as a parent watching a Button or an object drawn into this window. Each signal fires
    // after its hook. Keep the Connection for as long as the callback should run.
    Connection on_hover_changed(std::function<void(bool hovered)> callback) { return hoverChanged_.connect(std::move(callback)); }
    Connection on_focus_changed(std::function<void(bool focused)> callback) { return focusChanged_.connect(std::move(callback)); }
    Connection on_enabled_changed(std::function<void(bool enabled)> callback) { return enabledChanged_.connect(std::move(callback)); }
    Connection on_pointer_capture_lost(std::function<void()> callback) { return captureLost_.connect(std::move(callback)); }
    // Runs once when the native window is about to be destroyed: by DestroyWindow, with its
    // parent, or by this object's destructor. hwnd() is still valid during the callback.
    Connection on_destroy(std::function<void()> callback) { return destroying_.connect(std::move(callback)); }

    // The UI Automation provider returned for WM_GETOBJECT (UiaRootObjectId); null leaves the
    // system's default HWND provider. The window holds a reference until the provider is replaced
    // or the HWND is destroyed. Accessible sets this.
    void set_automation_provider(IRawElementProviderSimple*) noexcept;
    [[nodiscard]] IRawElementProviderSimple* automation_provider() const noexcept { return automationProvider_; }

    // Routes a child control's notifications to handler; nullptr removes the route. The control
    // must belong to the calling thread. Remove the route before the handler is destroyed.
    static void set_notification_handler(HWND control, NotificationHandler* handler);

    // Draws the title bar and frame of a top-level window dark or light, through DWM; child
    // windows throw E_INVALIDARG. Composia does not follow the system by itself: pass
    // application().appearance().dark to follow it, from on_appearance_changed as well. Windows
    // repaints the frame when it next draws it, so call it before showing the window. With "Show
    // accent color on title bars" on, Windows draws the accent color instead.
    void set_dark_title_bar(bool dark);

protected:
    [[nodiscard]] HWND require_hwnd() const;
    virtual void on_graphics_recreated() {}
    // Runs when the system appearance changes (see Application::appearance), before the window
    // is invalidated.
    virtual void on_appearance_changed() {}
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
    void update_hover(bool inside);
    void notify_capture_lost();
    void refresh_enabled();
    void propagate_enabled();
    void remember_focus(HWND) noexcept;
    bool restore_focus();
    void notify_destroy();

    Application& application_;
    bool topLevel_{};
    wil::unique_hwnd hwnd_;
    std::exception_ptr callbackError_;
    IRawElementProviderSimple* automationProvider_{};
    std::optional<RECT> paintRect_;
    HWND lastFocus_{};
    bool hovered_{}, tracking_{}, captured_{}, reportedEnabled_{true}, destroyNotified_{};
    Signal<bool> hoverChanged_, focusChanged_, enabledChanged_;
    Signal<> captureLost_, destroying_;
};

}
