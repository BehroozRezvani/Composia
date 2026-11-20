#pragma once

#include <composia/CompositionWindowTarget.hpp>
#include <composia/Element.hpp>
#include <composia/Window.hpp>

namespace composia {

// A window or child pane that hosts one retained tree on one Composition drawing surface.
// Composition-backed windows use shared hosting when top-level or when the parent has a shared target.
class ElementWindow : public Window {
public:
    ElementWindow(Application&, std::wstring_view title, int widthDip, int heightDip,
        std::shared_ptr<Element> root, HWND parent = nullptr);
    // Use Content::native to keep the HWND backing bitmap for a Win32 menu bar.
    // The retained tree still draws into its own standalone Composition target.
    ElementWindow(Application&, std::wstring_view title, int widthDip, int heightDip,
        Content, std::shared_ptr<Element> root, HWND parent = nullptr);
    ~ElementWindow() override;
    // Gives keyboard focus to this window and reveals the requested retained tab stop.
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
    winrt::com_ptr<struct ElementAutomation> automation_;
    ElementTree tree_;
    CompositionWindowTarget target_;
    bool pointerFocus_{};
    std::weak_ptr<Element> previousFocus_;
    Connection destroying_;
};

}
