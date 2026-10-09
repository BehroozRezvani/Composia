// Every public header, compiled and linked from an installed, relocated package.
#include <composia/Accessible.hpp>
#include <composia/AnimationHelpers.hpp>
#include <composia/Appearance.hpp>
#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <composia/Composition.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/GraphicsDevice.hpp>
#include <composia/Layout.hpp>
#include <composia/Log.hpp>
#include <composia/Native.hpp>
#include <composia/NativeControl.hpp>
#include <composia/Platform.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/ScreenCapture.hpp>
#include <composia/Signal.hpp>
#include <composia/SwapChainSurface.hpp>
#include <composia/TextLayout.hpp>
#include <composia/TextureSurface.hpp>
#include <composia/Version.hpp>
#include <composia/VirtualSurface.hpp>
#include <composia/Window.hpp>
#include <string_view>

static_assert(std::string_view{COMPOSIA_VERSION_STRING} == COMPOSIA_PACKAGE_VERSION, "The headers do not match the package version");
static_assert(COMPOSIA_VERSION == COMPOSIA_VERSION_MAJOR * 10000 + COMPOSIA_VERSION_MINOR * 100 + COMPOSIA_VERSION_PATCH);

int main(int argc, char**) {
    // The package's manifest is embedded as the application manifest.
    if (!FindResourceW(nullptr, MAKEINTRESOURCEW(1), MAKEINTRESOURCEW(24))) { return 2; }
    // Keep a runtime-dependent call so Release also checks transitive static linking.
    if (argc > 1) {
        composia::Application app{true};
        (void)composia::TextureSurface::supported(app.compositor(), app.graphics().d3d_device().get());
        (void)composia::ScreenCapture::supported();
        {
            composia::VirtualSurface document{app.graphics(), {1000000, 1000000}};
            document.update({0, 0, 100, 100}, [](auto& draw, const RECT&) { draw.context()->Clear(D2D1::ColorF(0)); });
            composia::Window window{app, L"Consumer", 320, 240};
            composia::CompositionWindowTarget target{app.compositor(), app.graphics(), window.hwnd()};
            target.root().Children().InsertAtTop(composia::animations::sprite(app.compositor(), {10, 10}, {255, 0, 0, 0}));
            composia::Button button{window, L"Action"};
            // The painter in docs/guide.md.
            composia::Button save{window, L"Save", [](composia::ScopedSurfaceDraw& draw, composia::numerics::float2,
                                                     const composia::Button::State& state) {
                draw.context()->Clear(D2D1::ColorF(state.pressed ? 0x005A9E : 0x0078D4));
                draw.context()->DrawTextLayout({0, 0}, state.label.layout().get(), draw.solid_brush(0xFFFFFF));
            }};
            window.set_dark_title_bar(app.appearance().dark);
            auto appearance = app.on_appearance_changed([](const composia::Appearance&) {});
            button.set_bounds({10, 10, 160, 44});
            composia::NativeControl check{window, L"BUTTON", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, L"Option"};
            check.set_colors(RGB(255, 255, 255), RGB(0, 0, 0));
            composia::Window panel{app, L"Panel", 100, 40, window.hwnd()};
            composia::Accessible accessible{panel, {.name = L"Panel"}};
            composia::TextLayout text{app.graphics().text_factory().get(), L"Text", 12};
            (void)text.metrics();
            composia::SwapChainSurface frame{app, {16, 16}};
            composia::set_log_handler(nullptr);
        }
        app.close();
    }
    return 0;
}
