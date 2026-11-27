#pragma once

#include <composia/Native.hpp>
#include <composia/Input.hpp>
#include <composia/Layout.hpp>
#include <composia/Signal.hpp>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

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
    enum class Content { native, composition };
    // How a window is made beyond its size, given at creation: Windows takes some of it only then
    // (an owner, WS_EX_NOREDIRECTIONBITMAP), and a change later redraws the frame.
    struct Options {
        // Replaces the default styles: WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN for a top-level window
        // (WS_POPUP for one without a frame, without the sizing and minimize styles for a dialog),
        // WS_CHILD | WS_TABSTOP | WS_CLIPSIBLINGS | WS_VISIBLE for a child, which keeps WS_CHILD.
        std::optional<DWORD> style;
        // Added to what the window needs: WS_EX_CONTROLPARENT for a top-level window, and
        // WS_EX_NOREDIRECTIONBITMAP for composition content.
        DWORD extendedStyle{};
        // A top-level window's owner, which it stays above and is minimized and destroyed with.
        HWND owner{};
        Content content{Content::native};
    };
    // Composition windows get their required redirection style at creation. Use a shared
    // CompositionWindowTarget to host resizable panes; native controls keep their own HWNDs.
    Window(Application&, std::wstring_view title, int widthDip, int heightDip, Content, HWND parent = nullptr);
    // Extra styles are applied at creation; some, such as WS_EX_NOREDIRECTIONBITMAP, cannot be added later.
    Window(Application&, std::wstring_view title, int widthDip, int heightDip, HWND parent = nullptr, DWORD extendedStyle = 0);
    Window(Application&, std::wstring_view title, int widthDip, int heightDip, const Options&, HWND parent = nullptr);
    virtual ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    void show(int command = SW_SHOWNORMAL);
    // Schedules a repaint of the whole client area, or of a DIP rectangle rounded out to whole
    // pixels. Requests coalesce into one WM_PAINT; on_paint reads the area from paint_rect().
    void invalidate();
    void invalidate(layout::Rect);
    void set_bounds(layout::Rect);
    // Moves a top-level window to the middle of another (its owner, say), kept on that window's
    // monitor's work area, at its current size.
    void center_on(HWND other);
    void rethrow_callback_error();
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_.get(); }
    [[nodiscard]] UINT dpi() const noexcept;
    [[nodiscard]] SIZE client_pixels() const;
    [[nodiscard]] bool top_level() const noexcept { return topLevel_; }
    // Whether keyboard focus is shown (a focus ring): from a key press anywhere in this window's
    // top-level window until a mouse button is pressed there, as the web's :focus-visible. A child
    // window shares its top-level window's. Native controls show their own focus.
    [[nodiscard]] bool focus_visible() const noexcept;
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

    // Key presses before anything else sees them. Application::run gives each WM_KEYDOWN and
    // WM_SYSKEYDOWN aimed at this window, or at any window inside it, to these handlers before
    // the dialog manager, the focused control or a retained tree. The innermost window's handlers
    // run first, in the order they were connected. A handler returns true to consume the key,
    // which then reaches nothing else and produces no character. Use it for accelerators, such
    // as Ctrl+N anywhere in a window, or arrow keys in a search field that move a list's
    // selection. Keep the Connection for as long as the handler should run.
    Connection on_key_preview(std::function<bool(const KeyEvent&)> handler);

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
    Window(Application&, std::wstring_view, int widthDip, int heightDip, HWND parent, const Options&, bool doubleClicks);
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
    // The top-level window's focus_visible() changed: repaint whatever shows the focus.
    virtual void on_focus_visible(bool) {}
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
    bool preview_key(const KeyEvent&);

    Application& application_;
    bool topLevel_{}, unredirected_{};
    bool focusVisible_{};   // A top-level window's focus cue.
    wil::unique_hwnd hwnd_;
    std::exception_ptr callbackError_;
    IRawElementProviderSimple* automationProvider_{};
    std::optional<RECT> paintRect_;
    HWND lastFocus_{};
    bool hovered_{}, tracking_{}, captured_{}, reportedEnabled_{true}, destroyNotified_{};
    bool paintPending_{}, painting_{};
    Signal<bool> hoverChanged_, focusChanged_, enabledChanged_;
    Signal<> captureLost_, destroying_;
    struct KeyPreview { std::shared_ptr<bool> active; std::function<bool(const KeyEvent&)> handler; };
    std::vector<std::shared_ptr<KeyPreview>> keyPreviews_;
};

// Disables a window while a dialog it owns is open, as a modal dialog does, and enables it again
// when the scope ends or end() is called, unless it was disabled already. Movable.
class ModalScope {
public:
    ModalScope() noexcept = default;
    explicit ModalScope(HWND owner) noexcept {
        if (owner && IsWindowEnabled(owner)) {
            EnableWindow(owner, FALSE);
            owner_ = owner;
        }
    }
    ~ModalScope() { end(); }
    ModalScope(ModalScope&& other) noexcept : owner_(std::exchange(other.owner_, nullptr)) {}
    ModalScope& operator=(ModalScope&& other) noexcept {
        if (this != &other) {
            end();
            owner_ = std::exchange(other.owner_, nullptr);
        }
        return *this;
    }
    ModalScope(const ModalScope&) = delete;
    ModalScope& operator=(const ModalScope&) = delete;
    // Enables the owner now, before the dialog hides, so it takes the activation back.
    void end() noexcept {
        if (const auto owner = std::exchange(owner_, nullptr); owner && IsWindow(owner)) { EnableWindow(owner, TRUE); }
    }
    [[nodiscard]] bool active() const noexcept { return owner_ != nullptr; }

private:
    HWND owner_{};
};

}
