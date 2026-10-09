#include <composia/Accessible.hpp>
#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/Log.hpp>
#include <composia/NativeControl.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/ScreenCapture.hpp>
#include <composia/SwapChainSurface.hpp>
#include <commctrl.h>
#include <uxtheme.h>
#include <UIAutomation.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Checks the mechanisms Window provides to every control and to content drawn into a window:
// pointer conversion, hover, capture, focus and its restoration, the enabled state and its
// propagation, invalidation and partial rendering, accessibility, and hosted native controls.
namespace {
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
constexpr UINT checkMessage = WM_APP + 77;
constexpr int skipped = 77;

class Probe final : public composia::Window {
public:
    Probe(composia::Application& app, std::wstring_view title, HWND parent = nullptr) : Window(app, title, 320, 240, parent) {}
    std::vector<std::string> events;
    composia::layout::Point lastPointer{};
    unsigned paints{};
    std::function<void()> check;
    std::function<void()> paint;
    std::function<void()> timer;

private:
    void on_hover(bool value) override { events.push_back(value ? "hover" : "leave"); }
    void on_focus(bool value) override { events.push_back(value ? "focus" : "blur"); }
    void on_capture_lost() override { events.push_back("capture-lost"); }
    void on_enabled(bool value) override { events.push_back(value ? "enabled" : "disabled"); }
    void on_paint() override { if (paint) { paint(); } }
    std::optional<LRESULT> on_message(UINT message, WPARAM, LPARAM lparam) override {
        if (message == checkMessage && check) { check(); return LRESULT{}; }
        if (message == WM_TIMER && timer) { timer(); return LRESULT{}; }
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
              .setValue = [this](std::wstring text) { value = text; accessible_.set_value(text); },
              .automationId = L"volume-knob", .localizedControlType = L"knob"}) {}
    composia::Accessible& accessibility() noexcept { return accessible_; }
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

template<class Action>
bool rejects(HRESULT expected, Action&& action) {
    try { action(); } catch (const wil::ResultException& error) { return error.GetErrorCode() == expected; }
    return false;
}

// Runs each step from the message loop, one posted message apiece, so the loop's own work, such
// as dialog navigation and focus tracking, happens between steps. The loop ends with the host.
void run_steps(composia::Application& app, Probe& host, std::vector<std::function<void()>> steps) {
    std::size_t next{};
    host.check = [&] {
        steps[next++]();
        if (host.hwnd()) { PostMessageW(host.hwnd(), next < steps.size() ? checkMessage : WM_CLOSE, 0, 0); }
    };
    PostMessageW(host.hwnd(), checkMessage, 0, 0);
    require(app.run() == 0, "The step loop failed");
}

// Native controls draw into the window's own surface, which PrintWindow copies; composition
// content is not part of it. Counts a control's light (or dark) pixels, skipping its left edge.
unsigned count_pixels(HWND top, HWND control, int skipLeft, bool light) {
    RECT window{}, area{};
    GetWindowRect(top, &window);
    GetWindowRect(control, &area);
    const int width = window.right - window.left, height = window.bottom - window.top;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits{};
    const wil::unique_hdc_window screen{GetDC(nullptr)};
    const wil::unique_hdc memory{CreateCompatibleDC(screen.get())};
    const wil::unique_hbitmap bitmap{CreateDIBSection(screen.get(), &info, DIB_RGB_COLORS, &bits, nullptr, 0)};
    require(memory && bitmap && bits, "Could not create a capture bitmap");
    const auto select = wil::SelectObject(memory.get(), bitmap.get());
    THROW_IF_WIN32_BOOL_FALSE(PrintWindow(top, memory.get(), 0));
    GdiFlush();
    const auto pixels = static_cast<const std::uint32_t*>(bits);
    unsigned matching{};
    for (LONG y = area.top - window.top; y < area.bottom - window.top; ++y) {
        for (LONG x = area.left - window.left + skipLeft; x < area.right - window.left; ++x) {
            const auto value = pixels[y * width + x];
            const unsigned channels[]{(value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF};
            if (std::ranges::all_of(channels, [&](unsigned channel) { return light ? channel > 200 : channel < 80; })) { ++matching; }
        }
    }
    return matching;
}

struct Pixel { int r{}, g{}, b{}; };

Pixel read_pixel(composia::Application& app, const composia::capture::Direct3D11CaptureFrame& frame, UINT x, UINT y) {
    const auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    wil::com_ptr<ID3D11Texture2D> texture;
    THROW_IF_FAILED(access->GetInterface(IID_PPV_ARGS(texture.put())));
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    desc.Width = desc.Height = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = desc.MiscFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    wil::com_ptr<ID3D11Texture2D> staging;
    THROW_IF_FAILED(app.graphics().d3d_device()->CreateTexture2D(&desc, nullptr, staging.put()));
    const D3D11_BOX box{x, y, 0, x + 1, y + 1, 1};
    const auto context = app.graphics().d3d_context().get();
    context->CopySubresourceRegion(staging.get(), 0, 0, 0, 0, texture.get(), 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    THROW_IF_FAILED(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));
    const auto bytes = static_cast<const unsigned char*>(mapped.pData);
    const Pixel pixel{bytes[2], bytes[1], bytes[0]};
    context->Unmap(staging.get(), 0);
    return pixel;
}

void pointer(bool warp) {
    composia::Application app{warp};
    {
        Probe host{app, L"Pointer host"};
        Probe child{app, L"Pointer child", host.hwnd()};
        child.set_bounds({10, 10, 200, 120});
        host.show();
        unsigned hoverSignals{}, captureSignals{};
        const auto hoverConnection = child.on_hover_changed([&](bool) { ++hoverSignals; });
        const auto captureConnection = child.on_pointer_capture_lost([&] { ++captureSignals; });
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

        // A captured pointer keeps sending moves from outside the client area, and Windows reports
        // no leave until the capture ends; hover follows the position instead.
        child.capture_pointer();
        SendMessageW(child.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(static_cast<WORD>(-5), static_cast<WORD>(-5)));
        require(!child.hovered() && count(child.events, "leave") == 2, "A captured move outside the client area kept the hover");
        SendMessageW(child.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(10, 10));
        require(child.hovered() && count(child.events, "hover") == 3, "Moving back inside during capture did not resume the hover");
        child.release_pointer();
        // A button press inside the client area counts as hovering too.
        SendMessageW(child.hwnd(), WM_MOUSELEAVE, 0, 0);
        SendMessageW(child.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
        SendMessageW(child.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
        require(child.hovered() && count(child.events, "hover") == 4, "A press inside the client area did not report hover");
        require(hoverSignals == 7, "on_hover_changed did not follow every hover change");

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
        require(captureSignals == 2, "on_pointer_capture_lost did not follow the capture losses");

        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(child.hwnd()));
        require(child.scale() == 1.0f && !child.hovered() && !child.pointer_captured(), "Destroyed window kept pointer state");
        require(rejects(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), [&] { child.capture_pointer(); }), "Destroyed window accepted capture");
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
        Probe inner{app, L"Inner", first.hwnd()};
        composia::Button button{host, L"Button"};
        first.set_bounds({10, 10, 100, 40});
        second.set_bounds({10, 60, 100, 40});
        inner.set_bounds({2, 2, 40, 20});
        button.set_bounds({150, 10, 120, 40});
        host.show();
        std::vector<bool> focusSignals, enabledSignals;
        const auto focusConnection = first.on_focus_changed([&](bool value) { focusSignals.push_back(value); });
        const auto enabledConnection = inner.on_enabled_changed([&](bool value) { enabledSignals.push_back(value); });
        first.focus();
        require(first.focused() && GetFocus() == first.hwnd() && count(first.events, "focus") == 1, "focus() did not focus the window");
        second.focus();
        require(!first.focused() && second.focused() && count(first.events, "blur") == 1 && count(second.events, "focus") == 1, "Moving focus was not reported to both windows");
        require(focusSignals == std::vector{true, false}, "on_focus_changed did not follow the focus");

        // enabled() includes the ancestors, and so do the notifications.
        require(first.enabled() && second.enabled() && host.enabled() && inner.enabled(), "Fresh windows are not enabled");
        first.set_enabled(false);
        require(!first.enabled() && count(first.events, "disabled") == 1 && IsWindowEnabled(first.hwnd()) == FALSE, "set_enabled(false) did not disable");
        require(!inner.enabled() && count(inner.events, "disabled") == 1, "A disabled parent did not notify its child");
        first.set_enabled(true);
        require(first.enabled() && count(first.events, "enabled") == 1 && count(inner.events, "enabled") == 1, "set_enabled(true) did not re-enable the window and its child");
        UpdateWindow(host.hwnd());
        UpdateWindow(button.hwnd());
        require(GetUpdateRect(button.hwnd(), nullptr, FALSE) == FALSE, "The button still had a repaint pending");
        host.set_enabled(false);
        require(!first.enabled() && !second.enabled() && IsWindowEnabled(first.hwnd()) != FALSE && has(host.events, "disabled"), "A disabled parent did not disable its children");
        require(count(first.events, "disabled") == 2 && count(second.events, "disabled") == 1 && count(inner.events, "disabled") == 2,
            "Disabling an ancestor was not reported to every descendant");
        require(!button.enabled() && GetUpdateRect(button.hwnd(), nullptr, FALSE) != FALSE, "The button did not repaint when its parent was disabled");
        // A window's own change stays silent while a disabled ancestor decides its state.
        second.set_enabled(false);
        second.set_enabled(true);
        require(count(second.events, "disabled") == 1 && count(second.events, "enabled") == 0, "A change hidden by a disabled ancestor was reported");
        host.set_enabled(true);
        require(first.enabled() && second.enabled() && count(second.events, "enabled") == 1 && count(inner.events, "enabled") == 2,
            "Re-enabling the parent did not restore and notify the children");
        require(enabledSignals == std::vector{false, true, false, true}, "on_enabled_changed did not follow the effective state");

        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(second.hwnd()));
        require(!second.focused() && !second.enabled(), "Destroyed window reported focus or enabled state");
        require(rejects(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), [&] { second.focus(); }), "Destroyed window accepted focus");
        require(rejects(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), [&] { second.set_enabled(true); }), "Destroyed window accepted an enabled-state change");

        // Activation returns the focus to the window that had it, also after minimizing, and only
        // while that window can still take it.
        composia::NativeControl edit{host, L"EDIT", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER};
        edit.set_bounds({10, 110, 200, 28});
        Probe other{app, L"Other window"};
        other.show(SW_SHOWNOACTIVATE);
        run_steps(app, host, {
            [&] { SetActiveWindow(host.hwnd()); edit.focus(); require(edit.focused(), "The edit control did not take the focus"); },
            [&] { SetActiveWindow(other.hwnd()); require(GetFocus() == other.hwnd(), "Activating another window left the focus behind"); },
            [&] { SetActiveWindow(host.hwnd()); require(edit.focused(), "Reactivation did not return the focus to the edit control"); },
            [&] { SendMessageW(host.hwnd(), WM_SYSCOMMAND, SC_MINIMIZE, 0); require(IsIconic(host.hwnd()) != FALSE, "The window did not minimize"); },
            [&] { SendMessageW(host.hwnd(), WM_SYSCOMMAND, SC_RESTORE, 0); require(edit.focused(), "Restoring did not return the focus to the edit control"); },
            [&] { host.focus(); },
            [&] { SetActiveWindow(other.hwnd()); },
            [&] { SetActiveWindow(host.hwnd()); require(host.focused(), "Reactivation took the focus from the window itself"); },
            [&] { edit.focus(); },
            [&] { SetActiveWindow(other.hwnd()); edit.show(false); },
            [&] {
                SetActiveWindow(host.hwnd());
                require(host.focused(), "Reactivation focused a hidden control");
                THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(other.hwnd()));
            },
        });
    }
    app.close();
}

void render(bool warp) {
    // Composia's diagnostic events reach the log handler; this one records them. It is set before
    // the Application exists and removed after it is gone.
    std::vector<std::pair<composia::LogLevel, std::string>> logged;
    const auto record = [&](composia::LogLevel level, std::string_view message) { logged.emplace_back(level, std::string{message}); };
    const auto was_logged = [&](composia::LogLevel level, std::string_view prefix) {
        return std::ranges::any_of(logged, [&](const auto& entry) { return entry.first == level && entry.second.starts_with(prefix); });
    };
    composia::set_log_handler(record);
    const auto unhook = wil::scope_exit([] { composia::set_log_handler(nullptr); });
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
        require(rejects(E_NOT_VALID_STATE, [&] {
            composia::ScopedSurfaceDraw draw{target.surface(), app.graphics(), window.dpi()};
            draw.finish();
            (void)draw.solid_brush(0xFFFFFF);
        }), "A finished scope handed out a brush");

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
        // A failing log handler does not interrupt the operation it reports.
        composia::set_log_handler([](composia::LogLevel, std::string_view) { throw std::runtime_error("log handler failure"); });
        const auto generation = app.graphics().generation();
        app.graphics().recreate();
        composia::set_log_handler(record);
        require(app.graphics().generation() == generation + 1, "A throwing log handler interrupted device replacement");
        {
            Probe other{app, L"Other"};
            require(rejects(E_INVALIDARG, [&] { (void)target.render(other, painter); }), "render accepted a window it was not created for");
            THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(other.hwnd()));
        }

        // Inside on_paint, an invalidated area is updated on its own once the surface holds a frame.
        std::vector<D2D1_RECT_F> updates;
        window.paint = [&] {
            (void)target.render(window, [&](composia::ScopedSurfaceDraw& draw, composia::numerics::float2) {
                updates.push_back(draw.update_bounds());
                draw.context()->Clear(D2D1::ColorF(0x203040));
            });
        };
        const auto scale = window.scale();
        const auto same = [](const D2D1_RECT_F& a, const D2D1_RECT_F& b) {
            return std::abs(a.left - b.left) < 0.01f && std::abs(a.top - b.top) < 0.01f && std::abs(a.right - b.right) < 0.01f && std::abs(a.bottom - b.bottom) < 0.01f;
        };
        const auto rounded_out = [&](float left, float top, float right, float bottom) {
            return D2D1_RECT_F{std::floor(left * scale) / scale, std::floor(top * scale) / scale, std::ceil(right * scale) / scale, std::ceil(bottom * scale) / scale};
        };
        const auto whole = [&](const D2D1_RECT_F& area) {
            const auto bounds = window.client_bounds();
            return same(area, {0, 0, bounds.width, bounds.height});
        };
        const auto repaint = [&] { UpdateWindow(window.hwnd()); };
        window.invalidate();
        repaint();
        require(updates.size() == 1 && whole(updates.back()), "A full invalidation did not repaint the whole surface");
        window.invalidate({10.5f, 12.25f, 30, 20});
        repaint();
        require(updates.size() == 2 && same(updates.back(), rounded_out(10.5f, 12.25f, 40.5f, 32.25f)), "An invalidated rectangle was not updated on its own");
        window.invalidate({10, 10, 10, 10});
        window.invalidate({100, 50, 10, 10});
        repaint();
        require(updates.size() == 3 && same(updates.back(), rounded_out(10, 10, 110, 60)), "Separate invalidations did not coalesce into one update");
        window.invalidate({-20, -20, 30, 30});
        repaint();
        require(updates.size() == 4 && same(updates.back(), rounded_out(0, 0, 10, 10)), "An update was not clipped to the client area");
        // A resize discards the surface content and a device replacement loses it; both repaint everything.
        THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(window.hwnd(), nullptr, 0, 0, 420, 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        ValidateRect(window.hwnd(), nullptr);
        window.invalidate({10, 10, 10, 10});
        repaint();
        require(updates.size() == 5 && whole(updates.back()), "A resized surface was only partly repainted");
        app.graphics().recreate();
        ValidateRect(window.hwnd(), nullptr);
        window.invalidate({10, 10, 10, 10});
        repaint();
        require(updates.size() == 6 && whole(updates.back()), "A surface from a replaced device was only partly repainted");
        require(rejects(E_INVALIDARG, [&] { window.invalidate({0, 0, -1, 5}); }), "A negative invalidation size was accepted");
        window.paint = nullptr;

        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(window.hwnd()));
        require(rejects(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), [&] { (void)target.render(window, painter); }), "render accepted a destroyed window");
        require(rejects(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), [&] { window.invalidate({0, 0, 1, 1}); }), "A destroyed window accepted an invalidation");
        require(app.run() == 0, "Render loop failed");
    }
    app.close();
    require(was_logged(composia::LogLevel::info, "event=graphics_device_created generation=1 driver="), "Device creation was not logged");
    require(was_logged(composia::LogLevel::warning, "event=draw_device_loss hresult=0x887A0005"), "The injected device loss was not logged");
    require(!logged.empty() && logged.back() == std::pair{composia::LogLevel::info, std::string{"event=application_shutdown"}}, "Shutdown was not logged last");
}

// Reads the composited result through a capture of the target's visual: partial repaints change
// only their own areas, and the surface keeps everything else.
int partial(bool warp) {
    if (!composia::ScreenCapture::supported()) {
        std::cout << "skipped: Windows Graphics Capture is unavailable\n";
        return skipped;
    }
    composia::Application app{warp};
    {
        Probe window{app, L"Partial rendering"};
        composia::CompositionWindowTarget target{app.compositor(), app.graphics(), window.hwnd()};
        UINT32 color = 0xFF0000;
        window.paint = [&] {
            // Clears everything; the update clip keeps it inside the area being repainted.
            (void)target.render(window, [&](composia::ScopedSurfaceDraw& draw, composia::numerics::float2) { draw.context()->Clear(D2D1::ColorF(color)); });
        };
        window.show();
        UpdateWindow(window.hwnd());
        composia::ScreenCapture capture;
        capture.start(composia::capture::GraphicsCaptureItem::CreateFromVisual(target.root()), app.graphics().d3d_device().get());
        const auto logical = target.logical_size();
        const composia::layout::Rect firstArea{20, 20, 40, 40}, secondArea{120, 60, 40, 40};
        const auto is = [](Pixel pixel, UINT32 rgb) {
            return std::abs(pixel.r - static_cast<int>((rgb >> 16) & 0xFF)) < 12 && std::abs(pixel.g - static_cast<int>((rgb >> 8) & 0xFF)) < 12 &&
                std::abs(pixel.b - static_cast<int>(rgb & 0xFF)) < 12;
        };
        int stage{}, polls{};
        window.timer = [&] {
            require(++polls < 300, "Captured frames never showed the expected content");
            auto frame = capture.next_frame();
            if (!frame) { return; }
            const auto close = wil::scope_exit([&] { frame.Close(); });
            const auto size = frame.ContentSize();
            const auto sample = [&](float x, float y) {
                return read_pixel(app, frame, static_cast<UINT>(x / logical.x * static_cast<float>(size.Width)), static_cast<UINT>(y / logical.y * static_cast<float>(size.Height)));
            };
            const auto first = sample(40, 40), second = sample(140, 80), outside = sample(logical.x - 20, logical.y - 20);
            if (stage == 0 && is(first, 0xFF0000) && is(second, 0xFF0000) && is(outside, 0xFF0000)) {
                color = 0x00FF00;
                window.invalidate(firstArea);
                stage = 1;
            } else if (stage == 1 && is(first, 0x00FF00)) {
                require(is(second, 0xFF0000) && is(outside, 0xFF0000), "A partial repaint changed pixels outside its area");
                color = 0x0000FF;
                window.invalidate(secondArea);
                stage = 2;
            } else if (stage == 2 && is(second, 0x0000FF)) {
                require(is(first, 0x00FF00) && is(outside, 0xFF0000), "A second partial repaint disturbed earlier content");
                std::cout << "partial repaints kept the content outside their areas; frames=" << polls << '\n';
                stage = 3;
                THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(window.hwnd()));
            }
        };
        THROW_LAST_ERROR_IF(SetTimer(window.hwnd(), 1, 30, nullptr) == 0);
        require(app.run() == 0, "Partial rendering loop failed");
        require(stage == 3, "The partial rendering stages did not complete");
        capture.close();
    }
    app.close();
    return 0;
}

// A swap chain in the visual tree: presented frames appear in the composited result, through a
// resize, a device replacement, and an injected device loss, and they stay there without being
// presented again.
int swapchain(bool warp) {
    if (!composia::ScreenCapture::supported()) {
        std::cout << "skipped: Windows Graphics Capture is unavailable\n";
        return skipped;
    }
    composia::Application app{warp};
    {
        Probe window{app, L"Swap chain"};
        composia::CompositionWindowTarget target{app.compositor(), app.graphics(), window.hwnd()};
        UINT32 canvas = 0x000000;
        window.paint = [&] {
            (void)target.render(window, [&](composia::ScopedSurfaceDraw& draw, composia::numerics::float2) { draw.context()->Clear(D2D1::ColorF(canvas)); });
        };
        window.show();
        UpdateWindow(window.hwnd());
        const auto scale = window.scale();

        require(rejects(E_INVALIDARG, [&] { composia::SwapChainSurface invalid{app, {0, 10}}; }), "An empty swap chain was accepted");
        require(rejects(E_INVALIDARG, [&] { composia::SwapChainSurface invalid{app, {10, 10}, DXGI_ALPHA_MODE_STRAIGHT}; }),
            "An alpha mode Composition does not support was accepted");
        {
            composia::SwapChainSurface translucent{app, {10, 10}, DXGI_ALPHA_MODE_PREMULTIPLIED};
            translucent.present([&](ID3D11RenderTargetView* view, ID3D11Texture2D*) {
                const float clear[]{0, 0, 0, 0};
                app.graphics().d3d_context()->ClearRenderTargetView(view, clear);
            });
        }

        composia::SwapChainSurface chain{app, {160, 100}};
        require(rejects(E_INVALIDARG, [&] { chain.resize({-1, 5}); }), "A negative swap chain size was accepted");
        require(rejects(E_INVALIDARG, [&] { chain.present({}); }), "A missing renderer was accepted");
        auto sprite = app.compositor().CreateSpriteVisual();
        sprite.Offset({20, 20, 0});
        sprite.Brush(chain.brush());
        const auto place = [&] {
            const auto size = chain.size();
            sprite.Size({static_cast<float>(size.cx) / scale, static_cast<float>(size.cy) / scale});
        };
        place();
        target.root().Children().InsertAtTop(sprite);
        unsigned presents{};
        const auto fill = [&](float red, float green, float blue) {
            chain.present([&](ID3D11RenderTargetView* view, ID3D11Texture2D*) {
                ++presents;
                const float color[]{red, green, blue, 1};
                app.graphics().d3d_context()->ClearRenderTargetView(view, color);
            });
        };
        fill(1, 0, 0);

        composia::ScreenCapture capture;
        capture.start(composia::capture::GraphicsCaptureItem::CreateFromVisual(target.root()), app.graphics().d3d_device().get());
        const auto logical = target.logical_size();
        // In DIPs: inside the first size, inside only the grown size, and on the canvas.
        const composia::layout::Point inside{20 + 40 / scale, 20 + 40 / scale}, grown{20 + 180 / scale, 20 + 110 / scale},
            outside{logical.x - 10, logical.y - 10};
        const auto is = [](Pixel pixel, UINT32 rgb) {
            return std::abs(pixel.r - static_cast<int>((rgb >> 16) & 0xFF)) < 12 && std::abs(pixel.g - static_cast<int>((rgb >> 8) & 0xFF)) < 12 &&
                std::abs(pixel.b - static_cast<int>(rgb & 0xFF)) < 12;
        };
        int stage{}, polls{};
        unsigned framesShown{};
        window.timer = [&] {
            require(++polls < 300, "Captured frames never showed the expected swap chain content");
            auto frame = capture.next_frame();
            if (!frame) { return; }
            const auto close = wil::scope_exit([&] { frame.Close(); });
            const auto size = frame.ContentSize();
            const auto sample = [&](composia::layout::Point point) {
                return read_pixel(app, frame, static_cast<UINT>(point.x / logical.x * static_cast<float>(size.Width)),
                    static_cast<UINT>(point.y / logical.y * static_cast<float>(size.Height)));
            };
            if (stage == 0 && is(sample(inside), 0xFF0000) && is(sample(outside), 0x000000)) {
                chain.resize({200, 120});
                place();
                fill(0, 1, 0);
                stage = 1;
            } else if (stage == 1 && is(sample(grown), 0x00FF00)) {
                require(is(sample(inside), 0x00FF00), "The resized swap chain was not filled");
                // Swap chains belong to their device; a replacement rebuilds this one on the next present.
                const auto previous = chain.swap_chain().get();
                app.graphics().recreate();
                capture.recreate(app.graphics().d3d_device().get());
                fill(0, 0, 1);
                require(chain.swap_chain().get() != previous, "A replaced device kept the old swap chain");
                stage = 2;
            } else if (stage == 2 && is(sample(inside), 0x0000FF)) {
                const auto generation = app.graphics().generation();
                bool injected{};
                chain.present([&](ID3D11RenderTargetView* view, ID3D11Texture2D*) {
                    if (!injected) { injected = true; throw winrt::hresult_error(DXGI_ERROR_DEVICE_REMOVED); }
                    ++presents;
                    const float magenta[]{1, 0, 1, 1};
                    app.graphics().d3d_context()->ClearRenderTargetView(view, magenta);
                });
                require(app.graphics().generation() == generation + 1, "A device loss while presenting did not replace the device");
                capture.recreate(app.graphics().d3d_device().get());
                stage = 3;
            } else if (stage == 3 && is(sample(inside), 0xFF00FF)) {
                // Change only the canvas: the swap chain keeps its last frame without another present.
                framesShown = presents;
                canvas = 0x404040;
                window.invalidate();
                stage = 4;
            } else if (stage == 4 && is(sample(outside), 0x404040)) {
                require(is(sample(inside), 0xFF00FF) && presents == framesShown, "The swap chain content needed another present to stay visible");
                std::cout << "swap chain frames composited through resize, device replacement, and device loss; presents=" << presents << '\n';
                stage = 5;
                THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(window.hwnd()));
            }
        };
        THROW_LAST_ERROR_IF(SetTimer(window.hwnd(), 1, 30, nullptr) == 0);
        require(app.run() == 0, "Swap chain loop failed");
        require(stage == 5, "The swap chain stages did not complete");
        capture.close();
    }
    app.close();
    return 0;
}
}

void accessible(bool warp) {
    composia::Application app{warp};
    {
        Probe host{app, L"Accessible host"};
        Knob knob{app, host.hwnd()};
        knob.set_bounds({10, 10, 200, 40});
        host.show();
        auto& access = knob.accessibility();
        require(host.automation_provider() == nullptr && knob.automation_provider() == access.provider().get(), "Windows did not report their providers");
        require(access.window() == &knob, "The provider does not name its window");
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
        require(text(property(UIA_AutomationIdPropertyId)) == L"volume-knob" && text(property(UIA_LocalizedControlTypePropertyId)) == L"knob",
            "The automation ID or localized control type was not reported");
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

        // An Accessible that outlives its Window object lets go of it and reports the element as gone.
        auto lone = std::make_unique<Probe>(app, L"Lone", host.hwnd());
        auto outliving = std::make_unique<composia::Accessible>(*lone, composia::Accessible::Options{.name = L"Lone"});
        const auto loneProvider = outliving->provider();
        lone.reset();
        wil::unique_variant loneName;
        require(outliving->window() == nullptr && loneProvider->GetPropertyValue(UIA_NamePropertyId, loneName.addressof()) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE),
            "An Accessible kept serving a destroyed Window object");
        outliving.reset();

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
                require(property(UIA_IsEnabledPropertyId).boolVal == VARIANT_FALSE && property(UIA_IsKeyboardFocusablePropertyId).boolVal == VARIANT_FALSE,
                    "The disabled state was not reported");
                require(invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED) && valuePattern->SetValue(L"1") == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED),
                    "A disabled element accepted actions");
                knob.set_enabled(true);
                host.set_enabled(false);
                require(invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTENABLED), "A disabled parent did not block invocation");
                host.set_enabled(true);
                bool duplicate{};
                try { composia::Accessible second{knob, {.name = L"Twice"}}; } catch (const std::logic_error&) { duplicate = true; }
                require(duplicate && knob.automation_provider() == access.provider().get(), "A second provider was attached to the same window");
                THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(knob.hwnd()));
                require(access.window() == nullptr && invoke->Invoke() == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE), "A destroyed window's provider invoked");
                wil::unique_variant gone;
                require(provider->GetPropertyValue(UIA_NamePropertyId, gone.addressof()) == static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE), "A destroyed window's provider answered");
                PostMessageW(host.hwnd(), WM_CLOSE, 0, 0);
            }), "Could not schedule the follow-up checks");
        }), "Could not schedule the accessibility checks");
        require(app.run() == 0, "Accessible loop failed");
    }
    app.close();
}

// A child control routed through the public notification handler, without NativeControl.
struct Recorder final : composia::NotificationHandler {
    std::vector<UINT> messages;
    std::optional<LRESULT> notification(UINT message, WPARAM, LPARAM) override {
        messages.push_back(message);
        return message == WM_COMMAND ? std::optional<LRESULT>{0} : std::nullopt;
    }
};

void native(bool warp) {
    composia::Application app{warp};
    {
        Probe host{app, L"Native host"};
        composia::NativeControl edit{host, L"EDIT", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL};
        composia::NativeControl notes{host, L"EDIT", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_MULTILINE | ES_WANTRETURN};
        composia::NativeControl check{host, L"BUTTON", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, L"Option"};
        composia::NativeControl combo{host, L"COMBOBOX", WS_CHILD | WS_VISIBLE | CBS_DROPDOWN, L"Choice"};
        composia::NativeControl list{host, WC_LISTVIEWW, WS_CHILD | WS_VISIBLE | LVS_REPORT};
        unsigned changed{};
        unsigned clicks{};
        std::vector<UINT> notifications;
        const auto changes = edit.on_command([&](UINT code) { if (code == EN_CHANGE) { ++changed; } });
        const auto clickCount = check.on_command([&](UINT code) { if (code == BN_CLICKED) { ++clicks; } });
        const auto listNotifications = list.on_notify([&](const NMHDR& header) { notifications.push_back(header.code); });
        edit.set_bounds({10, 10, 200, 28});
        check.set_bounds({10, 50, 200, 24});
        combo.set_bounds({10, 84, 200, 120});
        list.set_bounds({220, 10, 80, 60});
        notes.set_bounds({220, 120, 80, 40});
        host.show();
        require(app.post([&] {
            // Native window identity and placement in pixels.
            wchar_t className[32]{};
            GetClassNameW(edit.hwnd(), className, 32);
            require(_wcsicmp(className, L"Edit") == 0 && GetParent(edit.hwnd()) == host.hwnd(), "The EDIT was not created under the host");
            RECT rect{};
            GetWindowRect(edit.hwnd(), &rect);
            POINT origin{rect.left, rect.top};
            MapWindowPoints(nullptr, host.hwnd(), &origin, 1);
            const auto scale = host.scale();
            const auto close = [](float actual, float expected) { return std::abs(actual - expected) <= 1.0f; };
            require(close(static_cast<float>(origin.x), 10 * scale) && close(static_cast<float>(origin.y), 10 * scale)
                && close(static_cast<float>(rect.right - rect.left), 200 * scale) && close(static_cast<float>(rect.bottom - rect.top), 28 * scale),
                "The EDIT was not placed at the DIP bounds times the scale");

            // Text, and EN_CHANGE for both programmatic and typed changes.
            edit.set_text(L"abc");
            require(edit.text() == L"abc" && edit.send(WM_GETTEXTLENGTH) == 3, "Text was not set or not reported");
            edit.focus();
            require(edit.focused() && GetFocus() == edit.hwnd(), "The EDIT did not take focus");
            edit.send(EM_SETSEL, 3, 3);  // SetWindowText leaves the caret at the start.
            SendMessageW(edit.hwnd(), WM_CHAR, L'x', 0);
            require(edit.text() == L"abcx" && changed >= 1, "Typing did not update the text or raise EN_CHANGE");

            // Fonts: the default is Segoe UI 9pt at the parent's DPI, set_font replaces it, and a DPI
            // change notification builds it again for the new monitor.
            const auto dpi = static_cast<int>(host.dpi());
            const auto font = reinterpret_cast<HFONT>(edit.send(WM_GETFONT));
            require(font != nullptr, "The EDIT has no font");
            LOGFONTW logical{};
            GetObjectW(font, sizeof(logical), &logical);
            require(logical.lfHeight == -MulDiv(9, dpi, 72) && _wcsicmp(logical.lfFaceName, L"Segoe UI") == 0, "The default font is not Segoe UI 9pt");
            edit.set_font(L"Consolas", 12, FW_BOLD);
            const auto consolas = reinterpret_cast<HFONT>(edit.send(WM_GETFONT));
            require(consolas != nullptr, "set_font left the EDIT without a font");
            LOGFONTW updated{};
            GetObjectW(consolas, sizeof(updated), &updated);
            require(updated.lfHeight == -MulDiv(12, dpi, 72) && _wcsicmp(updated.lfFaceName, L"Consolas") == 0 && updated.lfWeight == FW_BOLD,
                "set_font did not apply the requested font");
            SendMessageW(edit.hwnd(), WM_DPICHANGED_AFTERPARENT, 0, 0);
            const auto refreshed = reinterpret_cast<HFONT>(edit.send(WM_GETFONT));
            LOGFONTW rebuilt{};
            GetObjectW(refreshed, sizeof(rebuilt), &rebuilt);
            require(refreshed != consolas && rebuilt.lfHeight == updated.lfHeight && _wcsicmp(rebuilt.lfFaceName, L"Consolas") == 0,
                "A DPI change notification did not rebuild the font");

            // Colors through the parent's WM_CTLCOLOREDIT handling.
            edit.set_colors(RGB(10, 20, 30), RGB(40, 50, 60));
            const wil::unique_hdc_window dc{GetDC(host.hwnd())};
            require(dc != nullptr, "GetDC failed");
            const auto color_request = [&](UINT message, HWND control) {
                return reinterpret_cast<HBRUSH>(SendMessageW(host.hwnd(), message, reinterpret_cast<WPARAM>(dc.get()), reinterpret_cast<LPARAM>(control)));
            };
            const auto brush_color = [](HBRUSH brush) { LOGBRUSH detail{}; GetObjectW(brush, sizeof(detail), &detail); return detail.lbColor; };
            const auto brush = color_request(WM_CTLCOLOREDIT, edit.hwnd());
            require(brush != nullptr && brush_color(brush) == RGB(40, 50, 60) && GetTextColor(dc.get()) == RGB(10, 20, 30), "The EDIT colors were not applied to the device context");
            edit.clear_colors();
            require(color_request(WM_CTLCOLOREDIT, edit.hwnd()) != brush, "clear_colors did not return the EDIT to the default brush");

            // Drawn pixels: light text on a dark background, for the EDIT and for a check box, which
            // gives up visual styles while colors are set because the theme would draw its text.
            edit.set_colors(RGB(250, 250, 250), RGB(20, 30, 40));
            edit.set_text(L"WWWWWW");
            check.set_colors(RGB(250, 250, 250), RGB(20, 30, 40));
            require(GetWindowTheme(check.hwnd()) == nullptr, "A colored check box kept visual styles");
            UpdateWindow(host.hwnd());
            const auto glyph = static_cast<int>(24 * scale);
            const auto editLight = count_pixels(host.hwnd(), edit.hwnd(), 0, true), labelLight = count_pixels(host.hwnd(), check.hwnd(), glyph, true);
            require(editLight > 20, "The EDIT text was not drawn in the requested color");
            require(labelLight > 8, "The check box label was not drawn in the requested color");
            check.clear_colors();
            require(GetWindowTheme(check.hwnd()) != nullptr, "clear_colors did not restore visual styles");
            UpdateWindow(host.hwnd());
            // Themed again, the label is dark text on the system's light background.
            const auto themedDark = count_pixels(host.hwnd(), check.hwnd(), glyph, false);
            require(themedDark > 8, "The check box label kept the requested color after clear_colors");
            std::cout << "light_pixels edit=" << editLight << " label=" << labelLight << " themed_label_dark=" << themedDark << '\n';

            // A drop-down combo box asks for colors from its edit field and from its separate list.
            combo.set_colors(RGB(1, 2, 3), RGB(4, 5, 6));
            COMBOBOXINFO parts{};
            parts.cbSize = sizeof(parts);
            THROW_IF_WIN32_BOOL_FALSE(GetComboBoxInfo(combo.hwnd(), &parts));
            const auto field = color_request(WM_CTLCOLOREDIT, parts.hwndItem), dropDown = color_request(WM_CTLCOLORLISTBOX, parts.hwndList);
            require(field && dropDown && brush_color(field) == RGB(4, 5, 6) && brush_color(dropDown) == RGB(4, 5, 6),
                "The combo box edit field or list did not get the combo box colors");

            // WM_NOTIFY from a common control reaches on_notify.
            list.focus();
            require(std::ranges::find(notifications, static_cast<UINT>(NM_SETFOCUS)) != notifications.end(), "NM_SETFOCUS did not reach on_notify");

            // A native checkbox: BM_CLICK notifies the parent and changes the check state.
            check.send(BM_CLICK);
            require(clicks == 1 && check.send(BM_GETCHECK) == BST_CHECKED && check.text() == L"Option", "The native checkbox did not click, check, or report its text");

            // Any child control can take its notifications through the public handler route.
            Recorder recorder;
            const wil::unique_hwnd raw{CreateWindowExW(0, L"BUTTON", L"Raw", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 220, 80, 80, 24,
                host.hwnd(), nullptr, GetModuleHandleW(nullptr), nullptr)};
            require(raw != nullptr, "Could not create a raw button");
            composia::Window::set_notification_handler(raw.get(), &recorder);
            SendMessageW(raw.get(), BM_CLICK, 0, 0);
            require(std::ranges::find(recorder.messages, static_cast<UINT>(WM_COMMAND)) != recorder.messages.end(), "A registered handler did not receive WM_COMMAND");
            composia::Window::set_notification_handler(raw.get(), nullptr);
            const auto received = recorder.messages.size();
            SendMessageW(raw.get(), BM_CLICK, 0, 0);
            require(recorder.messages.size() == received, "A removed handler still received notifications");
            require(rejects(E_INVALIDARG, [&] { composia::Window::set_notification_handler(nullptr, &recorder); }), "A handler was registered for no window");

            // Enabled and visible state follow the control and its parent.
            check.set_enabled(false);
            require(!check.enabled() && IsWindowEnabled(check.hwnd()) == FALSE, "set_enabled(false) did not disable the checkbox");
            check.set_enabled(true);
            host.set_enabled(false);
            require(!check.enabled(), "A disabled parent did not disable the checkbox");
            host.set_enabled(true);
            require(check.enabled(), "Re-enabling the parent did not enable the checkbox");
            check.show(false);
            require(!check.visible() && !IsWindowVisible(check.hwnd()), "show(false) did not hide the checkbox");
            check.show(true);
            require(check.visible(), "show(true) did not show the checkbox");

            // Tab goes through the application's dialog loop; each check runs after the focus move.
            // A multiline edit control hands Tab to its parent with WM_NEXTDLGCTL, as in a dialog.
            edit.focus();
            int tabs{};
            host.check = [&] {
                if (tabs++ == 0) {
                    require(GetFocus() == notes.hwnd(), "Tab did not move focus to the multiline edit control");
                    PostMessageW(notes.hwnd(), WM_KEYDOWN, VK_TAB, 0);
                    PostMessageW(host.hwnd(), checkMessage, 0, 0);
                    return;
                }
                require(GetFocus() == check.hwnd(), "Tab did not move focus out of the multiline edit control");
                THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(host.hwnd()));
                require(edit.hwnd() == nullptr && notes.hwnd() == nullptr && check.hwnd() == nullptr && combo.hwnd() == nullptr && list.hwnd() == nullptr,
                    "Destroying the parent did not clear the native controls");
                require(rejects(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), [&] { edit.set_text(L"x"); }), "A destroyed native control accepted text");
            };
            PostMessageW(edit.hwnd(), WM_KEYDOWN, VK_TAB, 0);
            PostMessageW(host.hwnd(), checkMessage, 0, 0);
        }), "Could not schedule native checks");
        require(app.run() == 0, "Native loop failed");
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
        else if (name == "partial") { return partial(warp); }
        else if (name == "swapchain") { return swapchain(warp); }
        else if (name == "accessible") { accessible(warp); }
        else if (name == "native") { native(warp); }
        else { throw std::runtime_error("Unknown test"); }
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
