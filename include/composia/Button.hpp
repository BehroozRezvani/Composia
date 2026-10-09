#pragma once

#include <composia/Window.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/Signal.hpp>
#include <composia/TextLayout.hpp>
#include <uiautomationcore.h>
#include <d2d1_1.h>

namespace composia {
namespace detail { struct ButtonState; }

class Button final : public Window {
public:
    Button(Window& parent, std::wstring_view label);
    ~Button() override;
    void enabled(bool value);
    [[nodiscard]] bool enabled() const noexcept;
    void invoke();
    Connection on_click(std::function<void()> callback) { return clicked_.connect(std::move(callback)); }
    [[nodiscard]] const wil::com_ptr<IRawElementProviderSimple>& accessibility_provider() const noexcept { return provider_; }
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
    void disconnect_provider() noexcept;
    void cancel_press();

    CompositionWindowTarget target_;
    TextLayout text_;
    std::shared_ptr<detail::ButtonState> state_;
    wil::com_ptr<IRawElementProviderSimple> provider_;
    Signal<> clicked_;
    bool inside_{};
    bool mousePressed_{};
    bool keyPressed_{};
};

}
