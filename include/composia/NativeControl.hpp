#pragma once

#include <composia/Window.hpp>
#include <composia/Signal.hpp>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace composia {

// Hosts a standard Win32 control (EDIT, BUTTON, COMBOBOX, STATIC, LISTBOX, ...) as a child of a
// Window: DIP bounds, a DPI-aware font that follows monitor changes, optional text and background
// colors, and WM_COMMAND and WM_NOTIFY notifications routed from the parent. The HWND stays
// available for everything else the control offers. Text input through an EDIT control gets IME
// composition, selection, clipboard, and UI Automation from the system; a common-controls v6
// manifest gives the controls their themed look.
class NativeControl {
public:
    NativeControl(Window& parent, std::wstring_view className, DWORD style, std::wstring_view text = {}, DWORD extendedStyle = 0);
    ~NativeControl();
    NativeControl(const NativeControl&) = delete;
    NativeControl& operator=(const NativeControl&) = delete;

    void set_bounds(layout::Rect);  // DIPs at the parent's DPI.
    void show(bool visible);
    [[nodiscard]] bool visible() const noexcept;
    void set_enabled(bool);
    [[nodiscard]] bool enabled() const noexcept;  // False when the control or any ancestor is disabled.
    void focus();
    [[nodiscard]] bool focused() const noexcept;
    [[nodiscard]] std::wstring text() const;
    void set_text(std::wstring_view);
    // Point size at 96 DPI, scaled to the parent's monitor and refreshed when the DPI changes.
    // The default is Segoe UI at 9 points.
    void set_font(std::wstring_view family, float pointSize, int weight = FW_NORMAL);
    // Colors for controls that ask through WM_CTLCOLOREDIT, WM_CTLCOLORSTATIC, WM_CTLCOLORBTN,
    // or WM_CTLCOLORLISTBOX. clear_colors returns to the system defaults.
    void set_colors(COLORREF text, COLORREF background);
    void clear_colors();
    [[nodiscard]] HWND hwnd() const noexcept { return hwnd_.get(); }
    [[nodiscard]] Window& parent() const noexcept { return parent_; }
    LRESULT send(UINT message, WPARAM wparam = 0, LPARAM lparam = 0) const;  // SendMessage; throws once the control is destroyed.
    // WM_COMMAND notification codes from this control: EN_CHANGE, BN_CLICKED, CBN_SELCHANGE, ...
    Connection on_command(std::function<void(UINT code)> callback) { return command_.connect(std::move(callback)); }
    // WM_NOTIFY headers from this control (common controls).
    Connection on_notify(std::function<void(const NMHDR&)> callback) { return notify_.connect(std::move(callback)); }

private:
    friend class Window;
    [[nodiscard]] static NativeControl* from(HWND) noexcept;
    static LRESULT CALLBACK subclass_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
    [[nodiscard]] HWND require_hwnd() const;
    void apply_font();
    void detach() noexcept;
    void command(UINT code);
    void notify(const NMHDR&);
    [[nodiscard]] std::optional<LRESULT> control_color(HDC);

    Window& parent_;
    wil::unique_hwnd hwnd_;
    wil::unique_hfont font_;
    wil::unique_hbrush background_;
    std::wstring family_{L"Segoe UI"};
    float pointSize_{9.0f};
    int weight_{FW_NORMAL};
    std::optional<COLORREF> textColor_, backgroundColor_;
    Signal<UINT> command_;
    Signal<const NMHDR&> notify_;
};

}
