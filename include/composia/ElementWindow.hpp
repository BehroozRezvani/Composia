#pragma once

#include <composia/CompositionWindowTarget.hpp>
#include <composia/Element.hpp>
#include <composia/Window.hpp>

namespace composia {

// A window or child pane that hosts one retained tree on one Composition drawing surface.
// Uses shared hosting when top-level or when the parent has a shared target.
class ElementWindow : public Window {
public:
    ElementWindow(Application&, std::wstring_view title, int widthDip, int heightDip,
        std::shared_ptr<Element> root, HWND parent = nullptr);
    ~ElementWindow() override;
    [[nodiscard]] ElementTree& elements() noexcept { return tree_; }
    [[nodiscard]] CompositionWindowTarget& composition_target() noexcept { return target_; }

protected:
    void on_resize() override;
    void on_paint() override;
    void on_focus(bool) override;
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;

private:
    friend struct TextBoxAutomation;
    winrt::com_ptr<struct TextBoxAutomation> editorAutomation_;
    ElementTree tree_;
    CompositionWindowTarget target_;
    bool pointerFocus_{};
    Connection destroying_;
};

}
