#include "TextureFrames.hpp"
#include <windows.graphics.directx.direct3d11.interop.h>

TextureFrames::TextureFrames(const composia::composition::Compositor& compositor, composia::GraphicsDevice& graphics, SIZE size)
    : size_(size) {
    THROW_HR_IF(E_INVALIDARG, size.cx <= 0 || size.cy <= 0);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(size.cx);
    desc.Height = static_cast<UINT>(size.cy);
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
    for (auto& frame : frames_) {
        wil::com_ptr<ID3D11Texture2D> texture;
        THROW_IF_FAILED(graphics.d3d_device()->CreateTexture2D(&desc, nullptr, texture.put()));
        THROW_IF_FAILED(graphics.d3d_device()->CreateRenderTargetView(texture.get(), nullptr, frame.target.put()));
        winrt::com_ptr<IInspectable> surface;
        THROW_IF_FAILED(CreateDirect3D11SurfaceFromDXGISurface(texture.query<IDXGISurface>().get(), surface.put()));
        frame.video = surface.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface>();
        frame.surface = std::make_unique<composia::TextureSurface>(compositor, texture.get());
    }
}

TextureFrames::Frame* TextureFrames::acquire() {
    for (auto& frame : frames_) {
        if (&frame != current_ && frame.surface->available()) { return &frame; }
    }
    return nullptr;
}

void TextureFrames::present(Frame& frame, const composia::composition::CompositionSurfaceBrush& brush) {
    brush.Surface(frame.surface->surface());
    current_ = &frame;
    ++presented_;
}
