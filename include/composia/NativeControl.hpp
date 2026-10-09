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
// colors, and WM_COMMAND and WM_NOTIFY notifications, through the public NotificationHandler
// route. The HWND stays available for everything else the control offers. Text input through an
// EDIT control gets IME composition, selection, clipboard, and UI Automation from the system; a
// common-controls v6 manifest gives the controls their themed look.
class NativeControl : private NotificationHandler {
public:
    NativeControl(Window& parent, std::wstring_view className, DWORD style, std::wstring_view text = {}, DWORD extendedStyle = 0);
    ~NativeControl();
    NativeControl(const NativeControl&) = delete;
    NativeControl& operator=(const NativeControl&) = delete;

    void set_bounds(layout::Rect);  // DIPs at the parent's DPI; call again after a DPI change.
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
    // Text and background colors for controls that ask through WM_CTLCOLOREDIT,
    // WM_CTLCOLORSTATIC, or WM_CTLCOLORLISTBOX: edit fields, static text, list boxes, combo boxes
    // including their edit field and drop-down list, and check boxes, radio buttons, and group
    // boxes. Visual styles draw the text of those three button kinds in the theme's color, so
    // they lose visual styles (taking the classic look) while colors are set. Push buttons draw
    // with system colors and use the background only around their edges; use BS_OWNERDRAW or a
    // composition Button for colored ones. clear_colors returns to the system defaults and
    // restores visual styles.
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
    static LRESULT CALLBACK subclass_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
    std::optional<LRESULT> notification(UINT, WPARAM, LPARAM) override;
    [[nodiscard]] HWND require_hwnd() const;
    [[nodiscard]] bool theme_colors_text() const noexcept;
    void apply_font();
    void detach() noexcept;
    [[nodiscard]] std::optional<LRESULT> control_color(HDC);

    Window& parent_;
    wil::unique_hwnd hwnd_;
    HWND list_{};  // A drop-down combo box's list, which is not a child of the combo box.
    wil::unique_hfont font_;
    wil::unique_hbrush background_;
    std::wstring family_{L"Segoe UI"};
    float pointSize_{9.0f};
    int weight_{FW_NORMAL};
    std::optional<COLORREF> textColor_, backgroundColor_;
    bool unthemed_{};
    Signal<UINT> command_;
    Signal<const NMHDR&> notify_;
};

}
