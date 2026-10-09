#pragma once

#include <composia/Window.hpp>
#include <UIAutomation.h>
#include <functional>
#include <memory>
#include <string>

namespace composia {
namespace detail { struct AccessibleState; }

// A UI Automation provider for a Window-based control. Attach one to a Window to expose a name, a
// control type, the focus and enabled state, and optionally the Invoke and Value patterns. The
// Window answers WM_GETOBJECT with it, raises focus and enabled changes, and disconnects it when
// the HWND is destroyed. UI Automation calls arrive on other threads: properties are answered from
// state kept under a lock, and actions are posted to the UI thread through the Application.
class Accessible {
public:
    struct Options {
        std::wstring name{};
        long controlType = UIA_CustomControlTypeId;
        std::function<void()> invoke{};                   // Enables the Invoke pattern.
        bool value = false;                               // Enables the Value pattern; set_value reports the text.
        std::function<void(std::wstring)> setValue{};     // Value pattern writes; absent means read-only.
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
    [[nodiscard]] Window& window() const noexcept { return window_; }

private:
    friend class Window;
    [[nodiscard]] LRESULT root_provider(WPARAM, LPARAM);
    void focus_changed(bool);
    void enabled_changed(bool);
    void disconnect() noexcept;

    Window& window_;
    std::shared_ptr<detail::AccessibleState> state_;
    wil::com_ptr<IRawElementProviderSimple> provider_;
};

}
