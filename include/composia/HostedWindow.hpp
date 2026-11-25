#pragma once

#include <composia/Element.hpp>
#include <composia/Native.hpp>

namespace composia {

class Window;

// A place in a retained tree for a child window: a NativeControl, a Composia Window that draws
// on its own, or the host window of a browser. The tree keeps the window over the element's
// bounds in the ElementWindow that hosts it. Ancestors clip it through a rectangular window
// region, which shared composition targets also follow. The window is hidden while the element
// is clipped or scrolled out of view, while it is detached, and while the tree has no window, and
// it is enabled and disabled with the element.
//
// The element is a tab stop where it is inserted. When Tab, Shift+Tab or focus_element reaches
// it, the window takes the keyboard focus. Tab and Shift+Tab in the window continue with the
// elements after and before it, unless the window asks for Tab itself through WM_GETDLGCODE.
// A window that moves focus out by other means, such as a browser raising MoveFocusRequested,
// or whose focus rests in a window inside it, calls leave().
class HostedWindow final : public Element {
public:
    // The window must be a child of the hosting ElementWindow when the tree places it; another
    // parent is reported as std::logic_error from the window's update. It is hidden until then.
    // The window may be destroyed before the element, which then holds nothing.
    explicit HostedWindow(HWND window);
    explicit HostedWindow(Window& window);
    ~HostedWindow() override;

    // The hosted window, or null once it has been destroyed.
    [[nodiscard]] HWND hwnd() const noexcept { return window_; }
    // Gives the keyboard focus to the next element in Tab order, or with backwards the previous
    // one, and returns whether focus moved. Past the last element it continues with the next tab
    // stop outside the hosting ElementWindow, or wraps when there is none, as Tab does.
    bool leave(bool backwards);

private:
    friend class ElementTree;
    friend class ElementWindow;
    static LRESULT CALLBACK subclass(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR) noexcept;
    void place(ElementWindow& host);
    bool take_focus();
    void hide() noexcept;
    void on_detaching() noexcept override;

    HWND window_{};
    RECT placed_{}, clip_{};
    bool clipped_{}, tabbing_{};
};

}
