#pragma once

#include <composia/Composition.hpp>
#include <d3d11_4.h>

namespace composia {

class TextureSurface {
public:
    TextureSurface(const composition::Compositor&, ID3D11Texture2D*,
        winrt::Windows::Graphics::DirectX::DirectXAlphaMode = winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Ignore);
    [[nodiscard]] static bool supported(const composition::Compositor&, ID3D11Device*);
    [[nodiscard]] bool available() const;
    [[nodiscard]] const wil::com_ptr<ID3D11Texture2D>& texture() const noexcept { return texture_; }
    [[nodiscard]] const composition::CompositionTexture& surface() const noexcept { return surface_; }

private:
    wil::com_ptr<ID3D11Texture2D> texture_;
    composition::CompositionTexture surface_{nullptr};
};

}
