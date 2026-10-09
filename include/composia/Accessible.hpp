#pragma once

#include <composia/Window.hpp>
#include <UIAutomation.h>
#include <functional>
#include <memory>
#include <string>

namespace composia {
namespace detail { struct AccessibleState; }

// A UI Automation provider for a Window-based control, built on the window's public signals and
// automation provider slot. Attach one to a Window to expose a name, a control type, the focus
// and enabled state (including disabled ancestors), and optionally the Invoke and Value patterns.
// It disconnects when the native window is destroyed or the Window object goes away, whichever
// comes first, after which the provider reports the element as unavailable. UI Automation calls
// arrive on other threads: properties are answered from state kept under a lock, and actions are
// posted to the UI thread through the Application.
class Accessible {
public:
    struct Options {
        std::wstring name{};
        long controlType = UIA_CustomControlTypeId;
        std::function<void()> invoke{};                   // Enables the Invoke pattern.
        bool value = false;                               // Enables the Value pattern; set_value reports the text.
        std::function<void(std::wstring)> setValue{};     // Value pattern writes; absent means read-only.
        std::wstring automationId{};                      // A stable identifier for tests and automation tools.
        std::wstring localizedControlType{};              // The spoken role; give one with UIA_CustomControlTypeId.
    };

    Accessible(Window&, Options);
    ~Accessible();
    Accessible(const Accessible&) = delete;
    Accessible& operator=(const Accessible&) = delete;

    void set_name(std::wstring);    // Raises a Name property change.
    void set_value(std::wstring);   // Current Value pattern text; raises a property change.
    void raise_invoked();           // Announces that an invocation happened, from the pointer or the keyboard as well.
    [[nodiscard]] std::wstring name() const;
    [[nodiscard]] std::wstring value() const;
    [[nodiscard]] const wil::com_ptr<IRawElementProviderSimple>& provider() const noexcept { return provider_; }
    // The window this provider serves; null once the native window or the Window object is gone.
    [[nodiscard]] Window* window() const noexcept { return window_; }

private:
    void focus_changed(bool);
    void enabled_changed(bool);
    void disconnect() noexcept;

    Window* window_;
    std::shared_ptr<detail::AccessibleState> state_;
    wil::com_ptr<IRawElementProviderSimple> provider_;
    Connection focusConnection_, enabledConnection_, destroyConnection_;
};

}
