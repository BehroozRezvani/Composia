#include <composia/Application.hpp>
#include <composia/Appearance.hpp>
#include <composia/Button.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/ScreenCapture.hpp>
#include "TestHooks.hpp"
#include "support/TestSupport.hpp"
#include <UIAutomation.h>
#include <dwmapi.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// The system appearance: what Appearance::current() reads, how changes reach windows and
// subscribers, the dark title bar, and Button's painters, including the default look in light,
// dark, and high contrast. Changes are simulated with the test library's stand-in appearance,
// since changing the real settings would change the desktop.
using namespace composia;
using testing::require;
using testing::rejects;

namespace {
constexpr UINT stepMessage = WM_APP + 21;

std::uint32_t rgb(COLORREF color) { return (GetRValue(color) << 16) | (GetGValue(color) << 8) | GetBValue(color); }

// Puts the system's appearance back in charge when it goes out of scope.
struct OverrideScope {
    ~OverrideScope() { detail::appearanceOverride.reset(); }
};

class Probe final : public Window {
public:
    Probe(Application& app, std::wstring_view title, HWND parent = nullptr) : Window(app, title, 320, 200, parent) {}
    unsigned appearanceChanges{};
    std::function<void()> onAppearance;
    std::function<void()> step;
    std::function<void()> timer;

private:
    void on_appearance_changed() override {
        ++appearanceChanges;
        if (onAppearance) { onAppearance(); }
    }
    std::optional<LRESULT> on_message(UINT message, WPARAM, LPARAM) override {
        if (message == stepMessage && step) { step(); return 0; }
        if (message == WM_TIMER && timer) { timer(); return 0; }
        return std::nullopt;
    }
};

// An appearance that differs from the system's in every field.
Appearance different(const Appearance& from) {
    Appearance result = from;
    result.dark = !from.dark;
    result.highContrast = !from.highContrast;
    result.animations = !from.animations;
    result.textScale = from.textScale + 0.25f;
    result.accent = from.accent ^ 0xFFFFFF;
    result.colors.window ^= 0xFFFFFF;
    return result;
}

int system_settings(const testing::Options& options) {
    Application app{options.warp};
    const auto now = Appearance::current();
    require(app.appearance() == now, "The application does not hold the current appearance");
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof(contrast);
    THROW_IF_WIN32_BOOL_FALSE(SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0));
    require(now.highContrast == ((contrast.dwFlags & HCF_HIGHCONTRASTON) != 0), "High contrast does not match the system");
    BOOL animations{};
    THROW_IF_WIN32_BOOL_FALSE(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0));
    require(now.animations == (animations != FALSE), "The animation setting does not match the system");
    require(now.colors.window == rgb(GetSysColor(COLOR_WINDOW)) && now.colors.windowText == rgb(GetSysColor(COLOR_WINDOWTEXT)) &&
        now.colors.buttonFace == rgb(GetSysColor(COLOR_BTNFACE)) && now.colors.buttonText == rgb(GetSysColor(COLOR_BTNTEXT)) &&
        now.colors.highlight == rgb(GetSysColor(COLOR_HIGHLIGHT)) && now.colors.highlightText == rgb(GetSysColor(COLOR_HIGHLIGHTTEXT)) &&
        now.colors.grayText == rgb(GetSysColor(COLOR_GRAYTEXT)) && now.colors.hotlight == rgb(GetSysColor(COLOR_HOTLIGHT)),
        "The system colors were not read as 0xRRGGBB");
    const winrt::Windows::UI::ViewManagement::UISettings settings;
    const auto foreground = settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Foreground);
    const auto accent = settings.GetColorValue(winrt::Windows::UI::ViewManagement::UIColorType::Accent);
    require(now.dark == (5 * foreground.G + 2 * foreground.R + foreground.B > 8 * 128), "Dark mode does not match UISettings");
    require(now.accent == ((static_cast<std::uint32_t>(accent.R) << 16) | (accent.G << 8) | accent.B), "The accent does not match UISettings");
    require(now.textScale == static_cast<float>(settings.TextScaleFactor()) && now.textScale >= 1.0f, "The text scale does not match UISettings");
    std::cout << "dark=" << now.dark << " high_contrast=" << now.highContrast << " animations=" << now.animations
              << " text_scale=" << now.textScale << " accent=0x" << std::hex << now.accent << '\n';
    app.close();
    return 0;
}

// Settings messages reach the application through any top-level window; it tells every window
// and every subscriber once, and only when the appearance changed.
int changes(const testing::Options& options) {
    const OverrideScope restore;
    Application app{options.warp};
    {
        Probe first{app, L"First"}, second{app, L"Second"};
        Probe child{app, L"Child", first.hwnd()};
        first.show(SW_SHOWNOACTIVATE);  // Hidden windows keep no update region to check.
        second.show(SW_SHOWNOACTIVATE);
        std::vector<Appearance> reported;
        unsigned alsoReported{};
        const auto subscription = app.on_appearance_changed([&](const Appearance& look) { reported.push_back(look); });
        const auto another = app.on_appearance_changed([&](const Appearance&) { ++alsoReported; });
        const auto counts = [&] { return std::vector{first.appearanceChanges, second.appearanceChanges, child.appearanceChanges}; };
        const auto validated = [&] { for (auto window : {&first, &second, &child}) { ValidateRect(window->hwnd(), nullptr); } };
        const auto invalidated = [&] {
            return GetUpdateRect(first.hwnd(), nullptr, FALSE) && GetUpdateRect(second.hwnd(), nullptr, FALSE) && GetUpdateRect(child.hwnd(), nullptr, FALSE);
        };
        const auto original = app.appearance();
        validated();
        SendMessageW(first.hwnd(), WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
        require(reported.empty() && counts() == std::vector{0u, 0u, 0u} && !invalidated(), "An unchanged appearance was reported");

        auto changed = different(original);
        detail::appearanceOverride = changed;
        SendMessageW(child.hwnd(), WM_SETTINGCHANGE, 0, 0);
        require(reported.empty() && app.appearance() == original, "A child window's settings message refreshed the appearance");
        SendMessageW(second.hwnd(), WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
        require(app.appearance() == changed && reported.size() == 1 && reported.back() == changed && alsoReported == 1,
            "The change was not reported once to every subscriber");
        require(counts() == std::vector{1u, 1u, 1u} && invalidated(), "Not every window was told and invalidated");
        SendMessageW(first.hwnd(), WM_SETTINGCHANGE, 0, 0);
        require(reported.size() == 1, "The same appearance was reported again");

        int index{};
        for (const UINT message : {WM_SYSCOLORCHANGE, WM_THEMECHANGED, WM_DWMCOLORIZATIONCOLORCHANGED}) {
            changed.accent = 0x102030 + static_cast<std::uint32_t>(index++);
            detail::appearanceOverride = changed;
            SendMessageW(first.hwnd(), message, 0, 0);
            require(reported.size() == 1 + static_cast<std::size_t>(index) && reported.back().accent == changed.accent,
                "A system color, theme, or colorization message did not refresh the appearance");
        }

        // A failing window does not keep the others or the subscribers from being told; its
        // error reaches the window that received the message.
        second.onAppearance = [] { throw std::runtime_error("appearance failure"); };
        changed.textScale += 0.25f;
        detail::appearanceOverride = changed;
        const auto before = reported.size();
        SendMessageW(first.hwnd(), WM_SETTINGCHANGE, 0, 0);
        require(child.appearanceChanges == 5 && reported.size() == before + 1, "A failing window stopped the notification");
        require(testing::throws<std::runtime_error>([&] { first.rethrow_callback_error(); }), "The failure was not reported");
        require(testing::throws<std::runtime_error>([&] { (void)app.run(); }), "The failure did not reach run()");
        second.onAppearance = {};
        for (auto window : {&child, &first, &second}) { DestroyWindow(window->hwnd()); }
    }
    app.close();
    return 0;
}

int title_bar(const testing::Options& options) {
    Application app{options.warp};
    {
        Probe window{app, L"Title bar"};
        Probe child{app, L"Child", window.hwnd()};
        const auto dark = [&] {
            BOOL value{};
            THROW_IF_FAILED(DwmGetWindowAttribute(window.hwnd(), DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value)));
            return value != FALSE;
        };
        window.set_dark_title_bar(true);
        require(dark(), "The title bar did not become dark");
        window.set_dark_title_bar(false);
        require(!dark(), "The title bar did not become light");
        require(rejects(E_INVALIDARG, [&] { child.set_dark_title_bar(true); }), "A child window accepted a title bar color");
        DestroyWindow(window.hwnd());
        require(rejects(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), [&] { window.set_dark_title_bar(true); }),
            "A destroyed window accepted a title bar color");
    }
    app.close();
    return 0;
}

// What a painter receives, and the painter, label, and pressed state Button exposes.
int button_painter(const testing::Options& options) {
    Application app{options.warp};
    {
        Probe host{app, L"Button painter"};
        struct Seen {
            bool hovered{}, pressed{}, focused{}, enabled{};
            const Appearance* appearance{};
            float labelWidth{};
            unsigned paints{};
        } seen;
        const Button::Painter recorder = [&](ScopedSurfaceDraw& draw, numerics::float2 size, const Button::State& state) {
            draw.context()->Clear(D2D1::ColorF(0xFF0000));
            seen = {state.hovered, state.pressed, state.focused, state.enabled, &state.appearance, state.label.layout()->GetMaxWidth(), seen.paints + 1};
            require(state.label.layout()->GetMaxWidth() == size.x, "The label was not sized to the button");
        };
        Button button{host, L"Painted", recorder};
        button.set_bounds({10, 10, 160, 44});
        host.show();
        const auto paint = [&] {
            UpdateWindow(button.hwnd());
            button.rethrow_callback_error();
        };
        paint();
        require(seen.paints >= 1 && seen.appearance == &app.appearance() && seen.enabled && !seen.hovered && !seen.pressed && !seen.focused,
            "The painter did not receive the initial state");
        SendMessageW(button.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(20, 20));
        paint();
        require(seen.hovered && !seen.pressed, "The painter did not see the hover");
        SendMessageW(button.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 20));
        paint();
        require(seen.pressed && button.pressed() && seen.focused, "The painter did not see the press and the focus");
        SendMessageW(button.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(static_cast<WORD>(-20), 20));
        paint();
        require(!seen.pressed && !button.pressed() && !seen.hovered, "Dragging off the button kept it pressed");
        SendMessageW(button.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(static_cast<WORD>(-20), 20));
        SendMessageW(button.hwnd(), WM_KEYDOWN, VK_SPACE, 0);
        paint();
        require(seen.pressed && button.pressed(), "Space did not press the button");
        SendMessageW(button.hwnd(), WM_KEYUP, VK_SPACE, 0);
        button.set_enabled(false);
        paint();
        require(!seen.enabled && !seen.pressed, "The painter did not see the disabled state");
        button.set_enabled(true);

        button.set_label(L"Renamed");
        wchar_t title[32]{};
        GetWindowTextW(button.hwnd(), title, 32);
        wil::unique_variant name;
        THROW_IF_FAILED(button.automation_provider()->GetPropertyValue(UIA_NamePropertyId, name.addressof()));
        require(button.label() == L"Renamed" && std::wstring_view{title} == L"Renamed" && name.vt == VT_BSTR &&
            std::wstring_view{name.bstrVal} == L"Renamed", "The label, window text, or UI Automation name was not changed");
        const auto paintsBefore = seen.paints;
        button.set_painter({});
        paint();
        require(seen.paints == paintsBefore, "An empty painter did not restore the default look");
        button.set_painter([&](ScopedSurfaceDraw& draw, numerics::float2 size, const Button::State& state) {
            Button::paint_default(draw, size, state);
            ++seen.paints;
        });
        paint();
        require(seen.paints == paintsBefore + 1, "A painter drawing over the default look did not run");
        DestroyWindow(host.hwnd());
        require(rejects(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), [&] { button.set_label(L"Gone"); }), "A destroyed button accepted a label");
    }
    app.close();
    return 0;
}

// The default look, read back from the composited button in each appearance.
int button_default(const testing::Options& options) {
    if (!ScreenCapture::supported()) {
        std::cout << "skipped: Windows Graphics Capture is unavailable\n";
        return testing::skipped;
    }
    const OverrideScope restore;
    Application app{options.warp};
    {
        Probe host{app, L"Button look"};
        Button button{host, L"Look"};
        button.set_bounds({10, 10, 160, 44});
        host.show();
        UpdateWindow(button.hwnd());
        ScreenCapture capture;
        capture.start(capture::GraphicsCaptureItem::CreateFromVisual(button.composition_target().root()), app.graphics().d3d_device().get());

        Appearance light = app.appearance(), dark = light, contrast = light;
        light.dark = light.highContrast = false;
        dark.dark = true;
        dark.highContrast = false;
        contrast.highContrast = true;
        contrast.colors.buttonFace = 0x101060;
        contrast.colors.buttonText = 0xF0F000;
        contrast.colors.highlight = 0x00A000;
        contrast.colors.highlightText = 0x000000;
        struct Step {
            const char* name;
            std::function<void()> setup;
            std::uint32_t face, border;
        };
        const auto use = [&](const Appearance& look) {
            detail::appearanceOverride = look;
            SendMessageW(host.hwnd(), WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
        };
        // Pressing with Space, which lasts until the key is released; a simulated hover would end
        // as soon as Windows noticed that the real pointer is elsewhere.
        const auto press = [&](bool down) {
            button.focus();
            SendMessageW(button.hwnd(), down ? WM_KEYDOWN : WM_KEYUP, VK_SPACE, down ? 0 : 0xC0000000);
        };
        const std::vector<Step> steps{
            {"light", [&] { use(light); }, 0xFDFDFD, 0xD1D1D1},
            {"light pressed", [&] { press(true); }, 0xE6E6E6, 0xD1D1D1},
            {"dark pressed", [&] { use(dark); }, 0x262626, 0x434343},
            {"dark", [&] { press(false); }, 0x2D2D2D, 0x434343},
            {"high contrast", [&] { use(contrast); }, 0x101060, 0xF0F000},
            {"high contrast pressed", [&] { press(true); }, 0x00A000, 0xF0F000},
        };
        std::size_t next{};
        bool ready{};
        int polls{};
        host.timer = [&] {
            require(++polls < 400, ("The button never showed its " + std::string{steps[next].name} + " look").c_str());
            if (!ready) { steps[next].setup(); ready = true; }
            auto frame = capture.next_frame();
            if (!frame) { return; }
            const auto close = wil::scope_exit([&] { frame.Close(); });
            const auto size = frame.ContentSize();
            const auto face = testing::frame_pixel(app, frame, static_cast<UINT>(size.Width) / 8, static_cast<UINT>(size.Height) / 4);
            const auto border = testing::frame_pixel(app, frame, 0, static_cast<UINT>(size.Height) / 2);
            if (!testing::matches(face, steps[next].face, 6) || !testing::matches(border, steps[next].border, 6)) { return; }
            std::cout << "look=" << steps[next].name << " frames=" << polls << '\n';
            ready = false;
            if (++next == steps.size()) { DestroyWindow(host.hwnd()); }
        };
        THROW_LAST_ERROR_IF(SetTimer(host.hwnd(), 1, 30, nullptr) == 0);
        require(app.run() == 0 && next == steps.size(), "The button looks did not complete");
        capture.close();
    }
    app.close();
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {
        {"system", system_settings},
        {"changes", changes},
        {"title-bar", title_bar},
        {"button-painter", button_painter},
        {"button-default", button_default},
    });
}
