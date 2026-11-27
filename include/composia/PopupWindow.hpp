#pragma once

#include <composia/ElementWindow.hpp>
#include <composia/Signal.hpp>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace composia {

// A popup in an owned top-level window of its own, so it can reach past its owner's edges: a
// menu, a dropdown's list or suggestions under a text field. It shows at a point or below a
// rectangle, flipped and shortened to stay on the monitor's work area, and closes on Escape,
// WM_CANCELMODE and losing the activation (a popup that takes the focus), at a press or the
// wheel outside it, when its owner moves, deactivates, is disabled or minimized, and with the
// popups it owns. The content is any element tree; the popup paints its
// background under it. Created hidden, it can be shown and closed again and again. UI thread only.
//
// It takes the keyboard (Activation::take_focus), as a menu does, or leaves it with its owner
// (keep_owner_focus), as a dropdown's list does while the field keeps routing keys to it. A popup
// that takes the focus keeps its owner's title bar active and gives the activation back when it
// closes. The popup is owned by the owner's top-level window.
//
// on_closed runs once per showing, last, possibly while the popup handles one of its own messages
// (Escape, say): do not destroy the popup from it then, post the destruction instead.
class PopupWindow : public ElementWindow {
public:
    enum class Activation { take_focus, keep_owner_focus };
    // Where a popup shown below a rectangle goes: from its left edge, as wide as it, or ending at
    // its right edge and kept inside the owner's window.
    enum class Below { leading, match_width, trailing_inside_owner };
    enum class Corners { round, round_small, square };
    enum class CloseReason { requested, escape, outside_press, anchor_press, deactivated, owner_changed, destroyed };
    struct Options {
        Activation activation{Activation::take_focus};
        Corners corners{Corners::round};
        std::uint32_t background{0xffffff};   // 0xRRGGBB under the content.
        float gap{4};                         // DIPs between a rectangle and the popup below or above it.
        bool menu{};                          // Raise UI Automation's MenuOpened and MenuClosed.
        // Close at a press or the wheel outside the popup, after the press reaches what it was on.
        bool lightDismiss{true};
        // A press on the rectangle the popup was shown below closes it and reaches nothing, so the
        // button there does not open it again on release. Off, the press goes on, and the button
        // can ask closed_by_anchor_press whether to open it again.
        bool consumeAnchorPress{true};
    };

    // `owner` is any window in the top-level window that will own the popup; `name` is the popup
    // window's title, which UI Automation reads.
    PopupWindow(Window& owner, std::shared_ptr<Element> content, std::wstring_view name = {});
    PopupWindow(Window& owner, std::shared_ptr<Element> content, std::wstring_view name, const Options&);
    // Hides the popup if it is open, without raising on_closed.
    ~PopupWindow() override;

    // Shows the popup with its size (DIPs) at a screen point (pixels): below and to the right of
    // it, or flipped left or up to stay on the work area, and no larger than the work area.
    void show_at(POINT screen, layout::Size size);
    // Shows it below a screen rectangle, such as a button, or above it when there is more room
    // there; it is shortened to the room on that side.
    void show_below(const RECT& anchor, layout::Size size, Below = Below::leading);
    void close();
    [[nodiscard]] bool is_open() const noexcept { return open_; }
    // Whether the popup closed at a press on the rectangle it was shown below, from that press
    // until the next press on this thread.
    [[nodiscard]] bool closed_by_anchor_press() const noexcept { return anchorPressed_; }
    [[nodiscard]] Activation activation() const noexcept { return options_.activation; }
    [[nodiscard]] HWND owner_hwnd() const noexcept { return owner_; }
    // The part of the owner's top-level window on the work area of the monitor at a screen point,
    // in pixels, and that monitor's DPI: what Below::trailing_inside_owner keeps the popup in.
    [[nodiscard]] RECT owner_area(POINT screen) const;
    [[nodiscard]] static UINT dpi_at(POINT screen) noexcept;
    void set_content(std::shared_ptr<Element>);
    void set_background(std::uint32_t rgb);
    // The border and shadow in the dark style, and the tooltips with them.
    void set_dark(bool);
    Connection on_closed(std::function<void(CloseReason)> callback) { return closed_.connect(std::move(callback)); }

protected:
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;
    [[nodiscard]] bool takes_focus() const noexcept override { return options_.activation == Activation::take_focus; }

private:
    class Surface;
    friend class PopupTracker;
    using Window::show;   // Showing goes through show_at and show_below.
    PopupWindow(Window& owner, std::shared_ptr<Surface> root, std::wstring_view name, const Options&);
    void place(RECT bounds);
    void finish_close(CloseReason) noexcept;
    // Closes after the current message, for this showing only; a stronger reason replaces a
    // weaker one already waiting.
    void close_later(CloseReason) noexcept;
    // Itself, a window inside it, or a window it owns, however deeply.
    [[nodiscard]] bool contains(HWND) const noexcept;
    // The window whose title bar shows the activation for this popup: its owner, or the owner's
    // owner past popups.
    [[nodiscard]] HWND frame_owner() const noexcept;
    void raise_menu_event(bool opened) noexcept;

    HWND owner_{};
    Options options_;
    Surface* surface_{};
    std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
    std::uint64_t presentation_{};
    std::optional<CloseReason> pending_;
    std::optional<RECT> anchor_;      // On the screen, while shown below it.
    RECT ownerRect_{};                // The owner's place when shown, to notice it moving.
    bool open_{}, placing_{}, closing_{}, anchorPressed_{};
    Signal<CloseReason> closed_;
};

}
