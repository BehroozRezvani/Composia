#include <composia/Window.hpp>
#include <composia/Accessible.hpp>
#include <composia/Application.hpp>
#include <composia/NativeControl.hpp>
#include <composia/TextLayout.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/AnimationHelpers.hpp>
#include <composia/Button.hpp>
#include <composia/TextureSurface.hpp>
#include <composia/ScreenCapture.hpp>
#include <composia/VirtualSurface.hpp>

int main(int argc, char**) {
    // Keep a runtime-dependent call so Release also checks transitive static linking.
    if (argc > 1) {
        composia::Application app{true};
        (void)composia::TextureSurface::supported(app.compositor(), app.graphics().d3d_device().get());
        (void)composia::ScreenCapture::supported();
        {
            composia::VirtualSurface document{app.graphics(), {1000000, 1000000}};
            document.update({0, 0, 100, 100}, [](auto& draw, const RECT&) { draw.context()->Clear(D2D1::ColorF(0)); });
            composia::Window window{app, L"Consumer", 320, 240};
            composia::Button button{window, L"Action"};
            button.set_bounds({10, 10, 160, 44});
            composia::NativeControl check{window, L"BUTTON", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, L"Option"};
            check.set_colors(RGB(255, 255, 255), RGB(0, 0, 0));
            composia::Window panel{app, L"Panel", 100, 40, window.hwnd()};
            composia::Accessible accessible{panel, {.name = L"Panel"}};
            composia::TextLayout text{app.graphics().text_factory().get(), L"Text", 12};
            (void)text.metrics();
        }
        app.close();
    }
    return 0;
}
