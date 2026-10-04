#include <composia/Application.hpp>
#include <composia/TextureSurface.hpp>
#include <iostream>
#include <stdexcept>

int main(int argc, char**) {
    try {
        composia::Application app{argc > 1};
        {
            auto& graphics = app.graphics();
            if (!composia::TextureSurface::supported(app.compositor(), graphics.d3d_device().get())) {
                std::cout << "Composition textures are unsupported on this device\n";
                return 77;
            }
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = desc.Height = 64;
            desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            wil::com_ptr<ID3D11Texture2D> texture;
            THROW_IF_FAILED(graphics.d3d_device()->CreateTexture2D(&desc, nullptr, texture.put()));
            composia::TextureSurface surface{app.compositor(), texture.get()};
            if (!surface.available()) { throw std::runtime_error("Fresh composition texture is not available"); }
            if (surface.texture().get() != texture.get()) { throw std::runtime_error("The wrapped texture was replaced"); }
            std::cout << "texture_created=true available=" << surface.available() << '\n';
        }
        app.close();
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
