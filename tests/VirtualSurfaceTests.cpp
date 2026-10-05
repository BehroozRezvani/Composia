#include <composia/Application.hpp>
#include <composia/VirtualSurface.hpp>
#include "VirtualDemoWindow.hpp"
#include <iostream>
#include <stdexcept>

using namespace composia;

namespace {
void require(bool value, const char* reason) { if (!value) { throw std::runtime_error(reason); } }

void pixel(Application& app, const composition::CompositionDrawingSurface& surface, LONG x, LONG y, bool red) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    wil::com_ptr<ID3D11Texture2D> texture, readback;
    THROW_IF_FAILED(app.graphics().d3d_device()->CreateTexture2D(&desc, nullptr, texture.put()));
    const RECT region{x, y, x + 1, y + 1};
    THROW_IF_FAILED(surface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop2>()->CopySurface(texture.get(), 0, 0, &region));
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    THROW_IF_FAILED(app.graphics().d3d_device()->CreateTexture2D(&desc, nullptr, readback.put()));
    const auto dc = app.graphics().d3d_context().get();
    dc->CopyResource(readback.get(), texture.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    THROW_IF_FAILED(dc->Map(readback.get(), 0, D3D11_MAP_READ, 0, &mapped));
    const auto unmap = wil::scope_exit([&] { dc->Unmap(readback.get(), 0); });
    const auto bytes = static_cast<const unsigned char*>(mapped.pData);
    require(bytes[3] == 255 && bytes[0] < 8 && (red ? bytes[2] > 240 && bytes[1] < 8 : bytes[1] > 240 && bytes[2] < 8),
        "Wrong pixels in retained virtual surface region");
}

void surface_checks(Application& app) {
    VirtualSurface surface{app.graphics(), {1 << 20, 1 << 20}, 256, 64};
    std::size_t paints{};
    bool red = true;
    const VirtualSurface::Painter paint = [&](ScopedSurfaceDraw& draw, const RECT&) {
        ++paints;
        draw.context()->Clear(D2D1::ColorF(red ? 0xFF0000 : 0x00FF00));
    };
    surface.update({0, 0, 400, 400}, paint);
    require(paints == 9, "Initial viewport did not draw 3x3 tiles including overscan");
    pixel(app, surface.surface(), 20, 20, true);
    surface.update({10, 10, 390, 390}, paint);
    require(paints == 9, "Cached tiles were unnecessarily redrawn");
    pixel(app, surface.surface(), 20, 20, true); // Trim must preserve the supplied region.
    red = false;
    surface.invalidate({1, 1, 40, 40});
    surface.update({10, 10, 390, 390}, paint);
    require(paints == 10, "Dirty region did not redraw exactly one tile");
    pixel(app, surface.surface(), 20, 20, false);
    pixel(app, surface.surface(), 300, 20, true);
    for (LONG i = 1; i <= 80; ++i) {
        const LONG offset = i * 12000;
        surface.update({offset, offset, offset + 400, offset + 400}, paint);
        // A 400-pixel view can cross three tiles, plus one overscan tile on either side.
        require(surface.cached_tiles() <= 25 && surface.retained_pixel_bytes() <= 25 * 256 * 256 * 4,
            "Virtual surface cache grew with scrolling history");
    }
    pixel(app, surface.surface(), 960010, 960010, false);
    const auto beforeReturn = paints;
    surface.update({0, 0, 400, 400}, paint);
    require(paints == beforeReturn + 9, "Trimmed tiles were reused without repainting");
    pixel(app, surface.surface(), 20, 20, false);
    const auto beforeRecovery = paints;
    app.graphics().recreate();
    surface.update({0, 0, 400, 400}, paint);
    require(paints == beforeRecovery + 9, "Device replacement reused lost tile contents");
    pixel(app, surface.surface(), 20, 20, false);
    bool rejected = false;
    try { surface.update({0, 0, 20000, 20000}, paint); } catch (...) { rejected = true; }
    require(rejected && surface.cached_tiles() == 9, "Oversized viewport did not preserve bounded cache");
    surface.clear();
    require(surface.cached_tiles() == 0 && surface.retained_pixel_bytes() == 0, "Clear retained tiles");
    bool failed = false;
    try { surface.update({0, 0, 200, 200}, [](auto&, const auto&) { throw std::runtime_error("paint failure"); }); }
    catch (const std::runtime_error&) { failed = true; }
    require(failed && surface.cached_tiles() == 0, "Failed paint marked an incomplete tile valid");
    surface.update({0, 0, 200, 200}, paint);
    pixel(app, surface.surface(), 20, 20, false);
    surface.resize({333, 271});
    surface.update({0, 0, 333, 271}, paint);
    require(surface.cached_tiles() == 4 && surface.retained_pixel_bytes() == 333 * 271 * 4,
        "Resize did not clip edge tiles");
    pixel(app, surface.surface(), 332, 270, false);
    surface.resize({VirtualSurface::maximum_dimension, VirtualSurface::maximum_dimension});
    const LONG edge = VirtualSurface::maximum_dimension;
    surface.update({edge - 200, edge - 200, edge, edge}, paint);
    pixel(app, surface.surface(), edge - 1, edge - 1, false);
    surface.update({-100, -100, 0, 0}, paint);
    require(surface.cached_tiles() == 0, "Empty viewport did not release cache");
    std::cout << "sparse_cache=true invalidation=true recovery=true maximum_extent=true\n";
}

void partial_checks(Application& app) {
    VirtualSurface surface{app.graphics(), {1 << 20, 1 << 20}};
    const RECT update{960000, 480000, 960128, 480128};
    for (UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
        ScopedSurfaceDraw draw{surface.surface(), app.graphics(), dpi, update};
        const auto dc = draw.context().get();
        dc->Clear(D2D1::ColorF(0x00FF00));
        wil::com_ptr<ID2D1SolidColorBrush> brush;
        THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0xFF0000), brush.put()));
        const float factor = 96.0f / dpi;
        dc->FillRectangle({(update.left + 16) * factor, (update.top + 16) * factor,
            (update.left + 80) * factor, (update.top + 80) * factor}, brush.get());
        draw.finish();
        pixel(app, surface.surface(), update.left + 32, update.top + 32, true);
        pixel(app, surface.surface(), update.left + 4, update.top + 4, false);
        std::cout << "partial_update_dpi=" << dpi << '\n';
    }
}

void demo_checks(Application& app) {
    VirtualDemoWindow window{app};
    window.show();
    const auto paint = [&] { UpdateWindow(window.hwnd()); window.rethrow_callback_error(); };
    const auto key = [&](WPARAM code) { SendMessageW(window.hwnd(), WM_KEYDOWN, code, 0); paint(); };
    paint();
    require(window.document().cached_tiles() > 0, "Demo did not draw initial viewport");
    key(VK_END);
    require(window.origin().x > 1000000 && window.origin().y > 1000000, "Far corner navigation failed");
    key(VK_HOME);
    require(window.origin().x == 0 && window.origin().y == 0, "Home navigation failed");
    key(VK_NEXT);
    require(window.origin().y > 100, "Page scroll failed");
    SendMessageW(window.hwnd(), WM_HSCROLL, SB_BOTTOM, 0); paint();
    require(window.origin().x > 1000000, "Native scrollbar failed");
    key(VK_OEM_MINUS);
    require(window.zoom() < 1, "Zoom out failed");
    for (int i = 0; i < 15; ++i) { key(VK_OEM_MINUS); }
    require(window.zoom() >= 0.25f && window.document().cached_tiles() <= 256, "Zoom out exceeded cache budget");
    SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(300, 220));
    const auto beforeDrag = window.origin().x;
    SendMessageW(window.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(350, 250));
    SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(350, 250)); paint();
    require(window.origin().x < beforeDrag, "Dragging did not pan");
    window.set_bounds({40, 40, 620, 460}); paint();
    window.show(SW_MINIMIZE);
    window.show(SW_RESTORE); paint();
    app.graphics().recreate(); paint();
    require(window.document().cached_tiles() > 0, "Demo recovery left empty tiles");
    SendMessageW(window.hwnd(), WM_CLOSE, 0, 0);
    window.rethrow_callback_error();
    std::cout << "demo_scroll=true zoom=true drag=true resize=true recovery=true\n";
}
}

int main(int argc, char**) {
    try {
        Application app{argc > 1};
        surface_checks(app);
        partial_checks(app);
        demo_checks(app);
        app.close();
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
