#include "Gpu.hpp"

struct Direct2D {
    wil::com_ptr<ID3D11Device> device;
    wil::com_ptr<ID3D11DeviceContext> context;
    wil::com_ptr<ID3D11Texture2D> surface, staging;
    wil::com_ptr<ID2D1RenderTarget> target;
    std::array<wil::com_ptr<ID2D1SolidColorBrush>, 3> brushes;
    Direct2D() {
        THROW_IF_FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            nullptr, 0, D3D11_SDK_VERSION, device.put(), nullptr, context.put()));
        surface = bench::texture(device.get(), bench::width, bench::height);
        staging = bench::texture(device.get(), 1, 1, true);
        wil::com_ptr<ID2D1Factory> factory;
        THROW_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory.put()));
        auto properties = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
        THROW_IF_FAILED(factory->CreateDxgiSurfaceRenderTarget(surface.query<IDXGISurface>().get(), properties, target.put()));
        for (size_t i = 0; i < brushes.size(); ++i) {
            THROW_IF_FAILED(target->CreateSolidColorBrush(D2D1::ColorF(bench::palette[i]), brushes[i].put()));
        }
    }
    unsigned paint(int frame) {
        target->BeginDraw();
        bench::rectangles(target.get(), frame, [&](int i) { return brushes[i].get(); });
        THROW_IF_FAILED(target->EndDraw());
        return bench::read(context.get(), staging.get(), surface.get(), 8, 8);
    }
};
int wmain(int argc, wchar_t** argv) { return bench::run<Direct2D>(argc, argv); }
