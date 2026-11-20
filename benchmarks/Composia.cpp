#include <composia/Application.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <windows.ui.composition.interop.h>
#include "Gpu.hpp"

struct Composition {
    composia::Application app;
    composia::GraphicsDevice& gfx = app.graphics();
    composia::composition::CompositionDrawingSurface surface = gfx.composition_device().CreateDrawingSurface(
        {bench::width, bench::height}, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
    winrt::com_ptr<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop2> interop =
        surface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop2>();
    wil::com_ptr<ID3D11Texture2D> sample = bench::texture(gfx.d3d_device().get(), 1, 1);
    wil::com_ptr<ID3D11Texture2D> staging = bench::texture(gfx.d3d_device().get(), 1, 1, true);
    unsigned paint(int frame) {
        composia::ScopedSurfaceDraw draw{surface, gfx, 96};
        bench::rectangles(draw.context().get(), frame, [&](int i) { return draw.solid_brush(bench::palette[i]); });
        draw.finish();
        const RECT pixel{8,8,9,9};
        THROW_IF_FAILED(interop->CopySurface(sample.get(), 0, 0, &pixel));
        return bench::read(gfx.d3d_context().get(), staging.get(), sample.get(), 0, 0);
    }
};
int wmain(int argc, wchar_t** argv) { return bench::run<Composition>(argc, argv); }
