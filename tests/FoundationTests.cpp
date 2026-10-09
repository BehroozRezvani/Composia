#include <composia/Accessible.hpp>
#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <UIAutomation.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Checks the mechanisms Window provides to every control: pointer conversion, hover, capture,
// focus, enabled state, and the one-call rendering helper.
namespace {
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }

class Probe final : public composia::Window {
public:
    Probe(composia::Application& app, std::wstring_view title, HWND parent = nullptr) : Window(app, title, 320, 240, parent) {}
    std::vector<std::string> events;
    composia::layout::Point lastPointer{};
    unsigned paints{};

private:
    void on_hover(bool value) override { events.push_back(value ? "hover" : "leave"); }
    void on_focus(bool value) override { events.push_back(value ? "focus" : "blur"); }
    void on_capture_lost() override { events.push_back("capture-lost"); }
    void on_enabled(bool value) override { events.push_back(value ? "enabled" : "disabled"); }
    std::optional<LRESULT> on_message(UINT message, WPARAM, LPARAM lparam) override {
        if (message == WM_MOUSEMOVE) { lastPointer = pointer_position(lparam); }
        if (message == WM_PAINT) { ++paints; }
        return std::nullopt;
    }
};

// A painted control exposing a name, the Invoke pattern, and a writable Value pattern.
class Knob final : public composia::Window {
public:
    Knob(composia::Application& app, HWND parent)
        : Window(app, L"Knob", 200, 40, parent),
          accessible_(*this, {.name = L"Volume", .controlType = UIA_SliderControlTypeId, .invoke = [this] { ++invoked; }, .value = true,
              .setValue = [this](std::wstring text) { value = text; accessible_.set_value(text); }}) {}
    unsigned invoked{};
    std::wstring value;

private:
    composia::Accessible accessible_;
};

bool has(const std::vector<std::string>& events, std::string_view name) {
    for (const auto& event : events) { if (event == name) { return true; } }
    return false;
}

unsigned count(const std::vector<std::string>& events, std::string_view name) {
    unsigned total{};
    for (const auto& event : events) { if (event == name) { ++total; } }
    return total;
}

void pointer(bool warp) {
    composia::Application app{warp};
    {
        Probe host{app, L"Pointer host"};
        Probe child{app, L"Pointer child", host.hwnd()};
        child.set_bounds({10, 10, 200, 120});
        host.show();
        const auto scale = child.scale();
        require(scale > 0.9f && std::abs(scale - static_cast<float>(child.dpi()) / 96.0f) < 0.001f, "Scale does not follow the DPI");
        const auto client = child.client_bounds();
        const auto pixels = child.client_pixels();
        require(std::abs(client.width * scale - static_cast<float>(pixels.cx)) < 0.6f && client.x == 0 && client.y == 0, "Client bounds are not the client area in DIPs");

        // Hover is tracked once per entry and reported on leave.
        require(!child.hovered() && child.events.empty(), "A fresh window reported hover");
        SendMessageW(child.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(24, 36));
        SendMessageW(child.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(48, 72));
        require(child.hovered() && count(child.events, "hover") == 1, "Hover was not reported exactly once");
        require(std::abs(child.lastPointer.x - 48 / scale) < 0.01f && std::abs(child.lastPointer.y - 72 / scale) < 0.01f, "Pointer position was not converted to DIPs");
        const auto fromScreen = [&] {
            POINT point{48, 72};
            ClientToScreen(child.hwnd(), &point);
            return child.pointer_position_from_screen(MAKELPARAM(point.x, point.y));
        }();
        require(std::abs(fromScreen.x - 48 / scale) < 0.01f && std::abs(fromScreen.y - 72 / scale) < 0.01f, "Screen coordinates were not converted");
        SendMessageW(child.hwnd(), WM_MOUSELEAVE, 0, 0);
        require(!child.hovered() && count(child.events, "leave") == 1, "Leaving was not reported");
        SendMessageW(child.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(1, 1));
        require(child.hovered() && count(child.events, "hover") == 2, "Re-entering was not reported");

        // Capture: explicit release is silent; losing it to another window or WM_CANCELMODE notifies.
        child.capture_pointer();
        require(child.pointer_captured() && GetCapture() == child.hwnd(), "capture_pointer did not take the capture");
        child.release_pointer();
        require(!child.pointer_captured() && GetCapture() == nullptr && !has(child.events, "capture-lost"), "release_pointer notified or kept the capture");
        child.capture_pointer();
        SetCapture(host.hwnd());
        require(!child.pointer_captured() && count(child.events, "capture-lost") == 1 && GetCapture() == host.hwnd(), "Losing capture to another window was not reported");
        ReleaseCapture();
        child.capture_pointer();
        SendMessageW(child.hwnd(), WM_CANCELMODE, 0, 0);
        require(!child.pointer_captured() && count(child.events, "capture-lost") == 2 && GetCapture() == nullptr, "WM_CANCELMODE did not release and report the capture");
        child.capture_pointer();
        SendMessageW(child.hwnd(), WM_CAPTURECHANGED, 0, reinterpret_cast<LPARAM>(child.hwnd()));
        require(child.pointer_captured() && count(child.events, "capture-lost") == 2, "A capture change to the same window counted as a loss");
        child.release_pointer();
        child.release_pointer();
        require(!child.pointer_captured(), "Repeated release is not idempotent");

        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(child.hwnd()));
        require(child.scale() == 1.0f && !child.hovered() && !child.pointer_captured(), "Destroyed window kept pointer state");
        bool rejected{};
        try { child.capture_pointer(); } catch (const wil::ResultException& error) { rejected = error.GetErrorCode() == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE); }
        require(rejected, "Destroyed window accepted capture");
        PostMessageW(host.hwnd(), WM_CLOSE, 0, 0);
        require(app.run() == 0, "Pointer loop failed");
    }
    app.close();
}

void focus(bool warp) {
    composia::Application app{warp};
    {
        Probe host{app, L"Focus host"};
        Probe first{app, L"First", host.hwnd()};
        Probe second{app, L"Second", host.hwnd()};
        first.set_bounds({10, 10, 100, 40});
        second.set_bounds({10, 60, 100, 40});
        host.show();
        first.focus();
        require(first.focused() && GetFocus() == first.hwnd() && count(first.events, "focus") == 1, "focus() did not focus the window");
        second.focus();
        require(!first.focused() && second.focused() && count(first.events, "blur") == 1 && count(second.events, "focus") == 1, "Moving focus was not reported to both windows");

        require(first.enabled() && second.enabled() && host.enabled(), "Fresh windows are not enabled");
        first.set_enabled(false);
        require(!first.enabled() && count(first.events, "disabled") == 1 && IsWindowEnabled(first.hwnd()) == FALSE, "set_enabled(false) did not disable");
        first.set_enabled(true);
        require(first.enabled() && count(first.events, "enabled") == 1, "set_enabled(true) did not re-enable");
        host.set_enabled(false);
        require(!first.enabled() && !second.enabled() && IsWindowEnabled(first.hwnd()) != FALSE && has(host.events, "disabled"), "A disabled parent did not disable its children");
        host.set_enabled(true);
        require(first.enabled() && second.enabled(), "Re-enabling the parent did not restore the children");

        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(second.hwnd()));
        require(!second.focused() && !second.enabled(), "Destroyed window reported focus or enabled state");
        bool rejected{};
        try { second.focus(); } catch (const wil::ResultException& error) { rejected = error.GetErrorCode() == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE); }
        require(rejected, "Destroyed window accepted focus");
        rejected = false;
        try { second.set_enabled(true); } catch (const wil::ResultException& error) { rejected = error.GetErrorCode() == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE); }
        require(rejected, "Destroyed window accepted an enabled-state change");
        PostMessageW(host.hwnd(), WM_CLOSE, 0, 0);
        require(app.run() == 0, "Focus loop failed");
    }
    app.close();
}

void render(bool warp) {
    composia::Application app{warp};
    {
        Probe window{app, L"Render"};
        composia::CompositionWindowTarget target{app.compositor(), app.graphics(), window.hwnd()};
        window.show();
        unsigned painted{};
        composia::numerics::float2 observed{};
        const auto painter = [&](composia::ScopedSurfaceDraw& draw, composia::numerics::float2 size) {
            ++painted;
            observed = size;
            const auto dc = draw.context().get();
            dc->Clear(D2D1::ColorF(0x101923));
            const auto first = draw.solid_brush(0x6FE6C8);
            const auto again = draw.solid_brush(0x6FE6C8);
            const auto other = draw.solid_brush(D2D1::ColorF(0x6FE6C8, 0.5f));
            require(first == again && first != other, "Scope brushes are not shared per color");
            dc->FillRectangle({8, 8, 40, 40}, first);
            dc->FillRectangle({48, 8, 80, 40}, other);
        };
        require(target.render(window, painter) && painted == 1, "render did not paint a visible window");
        const auto client = window.client_bounds();
        require(std::abs(observed.x - client.width) < 0.01f && std::abs(observed.y - client.height) < 0.01f, "render reported the wrong logical size");
        require(std::abs(target.logical_size().x - client.width) < 0.01f, "render did not size the surface to the client area");
        bool rejected{};
        try {
            composia::ScopedSurfaceDraw draw{target.surface(), app.graphics(), window.dpi()};
            draw.finish();
            (void)draw.solid_brush(0xFFFFFF);
        } catch (const wil::ResultException& error) { rejected = error.GetErrorCode() == E_NOT_VALID_STATE; }
        require(rejected, "A finished scope handed out a brush");

        ShowWindow(window.hwnd(), SW_MINIMIZE);
        require(!target.render(window, painter) && painted == 1, "render painted a minimized window");
        ShowWindow(window.hwnd(), SW_RESTORE);
        app.graphics().recreate();
        require(target.render(window, painter) && painted == 2, "render did not paint after device replacement");
        bool injected{};
        try {
            target.render(window, [&](composia::ScopedSurfaceDraw& draw, composia::numerics::float2 size) {
                if (!injected) { injected = true; throw winrt::hresult_error(DXGI_ERROR_DEVICE_REMOVED); }
                painter(draw, size);
            });
        } catch (...) { require(false, "render did not retry after an injected device loss"); }
        require(painted == 3, "The retry did not paint");
        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(window.hwnd()));
        rejected = false;
        try { (void)target.render(window, painter); } catch (const wil::ResultException& error) { rejected = error.GetErrorCode() == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE); }
        require(rejected, "render accepted a destroyed window");
        require(app.run() == 0, "Render loop failed");
    }
    app.close();
}
}

void accessible(bool warp) {
    composia::Application app{warp};
    {
        Probe host{app, L"Accessible host"};
        Knob knob{app, host.hwnd()};
        knob.set_bounds({10, 10, 200, 40});
        host.show();
        require(host.accessible() == nullptr && knob.accessible() != nullptr, "Windows did not report their providers");
        auto& access = *knob.accessible();
        const auto provider = access.provider();
        require(static_cast<bool>(provider), "The provider is missing");
        const auto property = [&](PROPERTYID id) {
            wil::unique_variant result;
            THROW_IF_FAILED(provider->GetPropertyValue(id, result.addressof()));
            return result;
        };
        const auto text = [](const wil::unique_variant& value) { return value.vt == VT_BSTR ? std::wstring{value.bstrVal, SysStringLen(value.bstrVal)} : std::wstring{}; };
        require(text(property(UIA_NamePropertyId)) == L"Volume" && access.name() == L"Volume", "The name was not reported");
        require(property(UIA_ControlTypePropertyId).lVal == UIA_SliderControlTypeId, "The control type was not reported");
        require(property(UIA_IsEnabledPropertyId).boolVal == VARIANT_TRUE && property(UIA_HasKeyboardFocusPropertyId).boolVal == VARIANT_FALSE, "Initial state is wrong");
        require(property(UIA_IsKeyboardFocusablePropertyId).boolVal == VARIANT_TRUE && property(UIA_IsControlElementPropertyId).boolVal == VARIANT_TRUE, "Element flags are wrong");
        knob.focus();
        require(property(UIA_HasKeyboardFocusPropertyId).boolVal == VARIANT_TRUE, "Focus was not reported");
        host.focus();
        require(property(UIA_HasKeyboardFocusPropertyId).boolVal == VARIANT_FALSE, "Losing focus was not reported");
        access.set_name(L"Loudness");
        require(text(property(UIA_NamePropertyId)) == L"Loudness", "Renaming was not reported");
        require(SendMessageW(knob.hwnd(), WM_GETOBJECT, 0, UiaRootObjectId) != 0 && SendMessageW(host.hwnd(), WM_GETOBJECT, 0, UiaRootObjectId) == 0,
            "WM_GETOBJECT was not answered for the right windows");
        wil::com_ptr<IRawElementProviderSimple> host_provider;
        THROW_IF_FAILED(provider->get_HostRawElementProvider(host_provider.put()));
        require(host_provider != nullptr, "The HWND host provider was not supplied");

        wil::com_ptr<IUnknown> invokeUnknown, valueUnknown, toggleUnknown;
        THROW_IF_FAILED(provider->GetPatternProvider(UIA_InvokePatternId, invokeUnknown.put()));
        THROW_IF_FAILED(provider->GetPatternProvider(UIA_ValuePatternId, valueUnknown.put()));
        THROW_IF_FAILED(provider->GetPatternProvider(UIA_TogglePatternId, toggleUnknown.put()));
        require(invokeUnknown && valueUnknown && !toggleUnknown, "Pattern availability is wrong");
        const auto invoke = invokeUnknown.query<IInvokeProvider>();
        const auto valuePattern = valueUnknown.query<IValueProvider>();
        BOOL readOnly{};
        THROW_IF_FAILED(valuePattern->get_IsReadOnly(&readOnly));
        wil::unique_bstr initial;
        THROW_IF_FAILED(valuePattern->get_Value(initial.put()));
        require(readOnly == FALSE && SysStringLen(initial.get()) == 0, "Initial value state is wrong");

        require(app.post([&] {
            THROW_IF_FAILED(invoke->Invoke());
            THROW_IF_FAILED(valuePattern->SetValue(L"42"));
            require(knob.invoked == 0 && knob.value.empty(), "Actions ran synchronously instead of through the application queue");
            require(app.post([&] {
                require(knob.invoked == 1 && knob.value == L"42" && access.value() == L"42", "Posted actions did not run on the UI thread");
                wil::unique_bstr now;
                THROW_IF_FAILED(valuePattern->get_Value(now.put()));
                require(std::wstring_view{now.get(), SysStringLen(now.get())} == L"42", "The Value pattern did not report the new value");
                knob.set_enabled(false);
                require(property(UIA_IsEnabledPropertyId).boolVal == VARIANT_FALSE, "The disabled state was not reported");
                require(invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED) && valuePattern->SetValue(L"1") == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED),
                    "A disabled element accepted actions");
                knob.set_enabled(true);
                host.set_enabled(false);
                require(invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED), "A disabled parent did not block invocation");
                host.set_enabled(true);
                bool duplicate{};
                try { composia::Accessible second{knob, {.name = L"Twice"}}; } catch (const std::logic_error&) { duplicate = true; }
                require(duplicate && knob.accessible() == &access, "A second provider was attached to the same window");
                THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(knob.hwnd()));
                require(invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE), "A destroyed window's provider invoked");
                wil::unique_variant gone;
                require(provider->GetPropertyValue(UIA_NamePropertyId, gone.addressof()) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE), "A destroyed window's provider answered");
                PostMessageW(host.hwnd(), WM_CLOSE, 0, 0);
            }), "Could not schedule the follow-up checks");
        }), "Could not schedule the accessibility checks");
        require(app.run() == 0, "Accessible loop failed");
    }
    app.close();
}

int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Expected a test name");
        const std::string_view name{argv[1]};
        const bool warp = argc > 2 && std::string_view{argv[2]} == "--warp";
        if (name == "pointer") { pointer(warp); }
        else if (name == "focus") { focus(warp); }
        else if (name == "render") { render(warp); }
        else if (name == "accessible") { accessible(warp); }
        else { throw std::runtime_error("Unknown test"); }
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
