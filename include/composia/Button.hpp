#pragma once

#include <composia/Accessible.hpp>
#include <composia/Appearance.hpp>
#include <composia/Window.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/Signal.hpp>
#include <composia/TextLayout.hpp>
#include <d2d1_1.h>
#include <functional>
#include <string>

namespace composia {

// A push button, built only on the public window, drawing, and accessibility mechanisms. It owns
// the behavior: pointer and keyboard activation, focus, the enabled state, and a UI Automation
// Invoke element. How it looks is up to its painter; without one it draws itself in the system's
// light, dark, or high contrast colors. It is a child window and a tab stop: place it with
// set_bounds, disable it with set_enabled, and reach its provider through automation_provider().
class Button final : public Window {
public:
    // The button as a painter draws it.
    struct State {
        TextLayout& label;             // The label, centered in the client area it is sized to.
        const Appearance& appearance;  // The application's current system appearance.
        bool hovered{};
        bool pressed{};                // Held by the pointer while over the button, or by Space.
        bool focused{};
        bool enabled{};                // False also while an ancestor is disabled.
    };
    // Draws the whole client area, whose size is in DIPs: a child window cannot show its parent
    // through it. Runs on every repaint, after a state change, a resize, an appearance change, or
    // a graphics device replacement.
    using Painter = std::function<void(ScopedSurfaceDraw&, numerics::float2 size, const State&)>;

    // The label is drawn centered and is the button's UI Automation name. An empty painter means
    // paint_default. The parent window must still have its HWND.
    Button(Window& parent, std::wstring_view label, Painter painter = {});
    ~Button() override;
    // Replaces the painter and repaints; an empty one restores paint_default.
    void set_painter(Painter painter);
    // Changes the label and the UI Automation name, and repaints.
    void set_label(std::wstring_view label);
    [[nodiscard]] const std::wstring& label() const noexcept { return label_; }
    [[nodiscard]] bool pressed() const noexcept;
    // Activates the button as a click, Space, Enter, or UI Automation Invoke does: raises the
    // Invoke event and then on_click. Does nothing while the button is disabled.
    void invoke();
    // Runs after each activation. Keep the Connection for as long as the callback should run.
    Connection on_click(std::function<void()> callback) { return clicked_.connect(std::move(callback)); }
    // The target the button paints into.
    [[nodiscard]] CompositionWindowTarget& composition_target() noexcept { return target_; }

    // The look of a button without a painter: a flat rectangle with a border and a focus outline,
    // in neutral light or dark colors, or in the system colors under high contrast. A painter can
    // call it and draw over it.
    static void paint_default(ScopedSurfaceDraw& draw, numerics::float2 size, const State& state);

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
    TextLayout make_label(std::wstring_view text) const;

    std::wstring label_;
    Painter painter_;
    CompositionWindowTarget target_;
    TextLayout text_;
    Accessible accessible_;
    Signal<> clicked_;
    bool mousePressed_{};
    bool keyPressed_{};
};

}
