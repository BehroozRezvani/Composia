#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
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

int main(int argc, char** argv) {
    try {
        require(argc >= 2, "Expected a test name");
        const std::string_view name{argv[1]};
        const bool warp = argc > 2 && std::string_view{argv[2]} == "--warp";
        if (name == "pointer") { pointer(warp); }
        else if (name == "focus") { focus(warp); }
        else if (name == "render") { render(warp); }
        else { throw std::runtime_error("Unknown test"); }
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
