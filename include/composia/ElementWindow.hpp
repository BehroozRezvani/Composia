#pragma once

#include <composia/CompositionWindowTarget.hpp>
#include <composia/Element.hpp>
#include <composia/Window.hpp>

namespace composia {

// A window or child pane that hosts one retained tree on one Composition drawing surface.
// Composition-backed windows use shared hosting when top-level or when the parent has a shared target.
// It shows element tooltips in a native tooltip window: below the pointer once it rests on an
// element for the system's hover time, and below the element when keyboard focus reaches it.
// A press, a key, the wheel, or the pointer leaving the element hides it; it shows again only
// after the pointer or focus moves to another element.
class ElementWindow : public Window {
public:
    ElementWindow(Application&, std::wstring_view title, int widthDip, int heightDip,
        std::shared_ptr<Element> root, HWND parent = nullptr);
    // Use Content::native to keep the HWND backing bitmap for a Win32 menu bar.
    // The retained tree still draws into its own standalone Composition target.
    ElementWindow(Application&, std::wstring_view title, int widthDip, int heightDip,
        Content, std::shared_ptr<Element> root, HWND parent = nullptr);
    ~ElementWindow() override;
    // Gives keyboard focus to this window and reveals the requested retained tab stop. For a
    // HostedWindow element, the focus goes to its window.
    bool focus_element(Element&);
    [[nodiscard]] ElementTree& elements() noexcept { return tree_; }
    [[nodiscard]] CompositionWindowTarget& composition_target() noexcept { return target_; }

protected:
    void on_resize() override;
    void on_paint() override;
    void on_focus(bool) override;
    void on_enabled(bool) override;
    void on_hover(bool) override;
    void on_capture_lost() override;
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;

private:
    friend struct ElementAutomation;
    friend class HostedWindow;
    // The tab stop outside this window that Tab or Shift+Tab reaches from it, skipping windows
    // its tree hosts; null when there is none.
    [[nodiscard]] HWND outside_tab_stop(bool backwards) const;
    void hand_focus_to_hosted_window();
    // Starts the tooltip delay for an element, or hides the tooltip when it changes.
    void follow_tooltip(Element* source, bool keyboard);
    void show_tooltip();
    void hide_tooltip() noexcept;
    void refresh_tooltip();
    winrt::com_ptr<struct ElementAutomation> automation_;
    ElementTree tree_;
    CompositionWindowTarget target_;
    bool pointerFocus_{};
    std::weak_ptr<Element> previousFocus_;
    wil::unique_hwnd tooltip_;
    std::weak_ptr<Element> tipSource_;
    std::wstring tipText_;
    layout::Point lastPointer_{};
    bool tipShown_{}, tipKeyboard_{};
    Connection destroying_;
};

}
