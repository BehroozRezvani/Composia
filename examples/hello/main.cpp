// The smallest complete Composia program: a window whose client area is drawn with Direct2D into
// a Composition surface, with a Button that closes it. It follows the system's light, dark, or
// high contrast appearance, as the Button's default look does.
#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/TextLayout.hpp>
#include <algorithm>
#include <cstdint>
#include <exception>

class HelloWindow final : public composia::Window {
public:
    explicit HelloWindow(composia::Application& app)
        : Window(app, L"Hello, Composia", 480, 240),
          target_(app.compositor(), app.graphics(), hwnd()),
          greeting_(app.graphics().text_factory().get(), L"Hello from Windows Composition", 24),
          close_(*this, L"Close") {
        clicked_ = close_.on_click([this] { PostMessageW(hwnd(), WM_CLOSE, 0, 0); });
        arrange();
        set_dark_title_bar(app.appearance().dark);
    }

private:
    // Child windows are placed in DIPs; Windows does not move them when the window resizes.
    void arrange() {
        const auto bounds = client_bounds();
        close_.set_bounds({std::max(24.0f, bounds.width - 184), std::max(80.0f, bounds.height - 68), 160, 44});
    }
    void on_resize() override {
        arrange();
        invalidate();
    }
    // The window is repainted after this, so only the frame needs updating here.
    void on_appearance_changed() override { set_dark_title_bar(application().appearance().dark); }
    // Paints the client area in DIPs; render sizes the surface and retries after device loss.
    void on_paint() override {
        const auto& look = application().appearance();
        const std::uint32_t background = look.highContrast ? look.colors.window : look.dark ? 0x202020 : 0xF3F3F3;
        const std::uint32_t text = look.highContrast ? look.colors.windowText : look.dark ? 0xFFFFFF : 0x1B1B1B;
        target_.render(*this, [&](composia::ScopedSurfaceDraw& draw, composia::numerics::float2 size) {
            draw.context()->Clear(D2D1::ColorF(background));
            greeting_.resize(std::max(1.0f, size.x - 48), 40);
            draw.context()->DrawTextLayout({24, 24}, greeting_.layout().get(), draw.solid_brush(text));
        });
    }

    composia::CompositionWindowTarget target_;
    composia::TextLayout greeting_;
    composia::Button close_;
    composia::Connection clicked_;
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int showCommand) {
    try {
        composia::Application app;
        int result{};
        {
            HelloWindow window{app};
            window.show(showCommand);
            result = app.run();
        }
        app.close();
        return result;
    } catch (const std::exception&) {
    } catch (const winrt::hresult_error&) {
    }
    MessageBoxW(nullptr, L"Composia could not continue.", L"Hello, Composia", MB_OK | MB_ICONERROR);
    return 1;
}
