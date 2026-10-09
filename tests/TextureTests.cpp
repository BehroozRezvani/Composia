#include <composia/Application.hpp>
#include <composia/TextureSurface.hpp>
#include "support/TestSupport.hpp"
#include <iostream>

// A Direct3D texture in the visual tree, where the runtime and device support composition textures.
using namespace composia;
using testing::require;

namespace {
int texture(const testing::Options& options) {
    Application app{options.warp};
    auto& graphics = app.graphics();
    require(testing::rejects(E_INVALIDARG, [&] { (void)TextureSurface::supported(app.compositor(), nullptr); }),
        "A missing device was accepted");
    if (!TextureSurface::supported(app.compositor(), graphics.d3d_device().get())) {
        std::cout << "skipped: composition textures are unsupported on this device\n";
        return testing::skipped;
    }
    {
        require(testing::rejects(E_INVALIDARG, [&] { TextureSurface surface{app.compositor(), nullptr}; }), "A missing texture was accepted");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = desc.Height = 64;
        desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        wil::com_ptr<ID3D11Texture2D> texture;
        THROW_IF_FAILED(graphics.d3d_device()->CreateTexture2D(&desc, nullptr, texture.put()));
        TextureSurface surface{app.compositor(), texture.get(), winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied};
        require(surface.available(), "A fresh composition texture is not available");
        require(surface.texture().get() == texture.get(), "The wrapped texture was replaced");
        require(surface.surface() && surface.surface().AlphaMode() == winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied,
            "The composition texture did not take the alpha mode");
        // The texture shows through an ordinary surface brush.
        const auto brush = app.compositor().CreateSurfaceBrush(surface.surface());
        require(brush.Surface() == surface.surface(), "A surface brush did not accept the composition texture");
        std::cout << "texture_created=true available=true\n";
    }
    app.close();
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"surface", texture}});
}
