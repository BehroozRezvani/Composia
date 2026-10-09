#pragma once

#include <composia/Accessible.hpp>
#include <composia/Window.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/Signal.hpp>
#include <composia/TextLayout.hpp>
#include <d2d1_1.h>

namespace composia {

// A composition-rendered push button, built only on the public window, drawing, and
// accessibility mechanisms: pointer and keyboard activation, focus ring, enabled state, and a
// UI Automation Invoke provider. It is a child window and a tab stop: place it with set_bounds,
// disable it with set_enabled, and reach its provider through automation_provider().
class Button final : public Window {
public:
    // The label is drawn centered and is the button's UI Automation name. The parent window must
    // still have its HWND.
    Button(Window& parent, std::wstring_view label);
    ~Button() override;
    // Activates the button as a click, Space, Enter, or UI Automation Invoke does: raises the
    // Invoke event and then on_click. Does nothing while the button is disabled.
    void invoke();
    // Runs after each activation. Keep the Connection for as long as the callback should run.
    Connection on_click(std::function<void()> callback) { return clicked_.connect(std::move(callback)); }
    // The target the button paints into.
    [[nodiscard]] CompositionWindowTarget& composition_target() noexcept { return target_; }

private:
    void on_resize() override;
    void on_paint() override;
    void on_hover(bool) override;
    void on_focus(bool) override;
    void on_capture_lost() override;
    void on_enabled(bool) override;
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;
    bool hit_test(LPARAM) const;
    void cancel_press();

    CompositionWindowTarget target_;
    TextLayout text_;
    Accessible accessible_;
    Signal<> clicked_;
    bool mousePressed_{};
    bool keyPressed_{};
};

}
