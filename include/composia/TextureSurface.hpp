#pragma once

#include <composia/Composition.hpp>
#include <d3d11_4.h>

namespace composia {

// A Direct3D 11 texture shown in the visual tree without a copy, as a CompositionTexture that a
// surface brush can use. It needs a recent Windows 11 runtime and a graphics device that supports
// composition textures; on Windows 10, use SwapChainSurface instead. Recreate the texture and the
// surface after a graphics device replacement.
class TextureSurface {
public:
    // Wraps the texture, which must belong to a device supported() accepts; otherwise throws
    // HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED).
    TextureSurface(const composition::Compositor&, ID3D11Texture2D*,
        winrt::Windows::Graphics::DirectX::DirectXAlphaMode = winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Ignore);
    // True when this Windows runtime and the device support composition textures.
    [[nodiscard]] static bool supported(const composition::Compositor&, ID3D11Device*);
    // True once the compositor has finished reading the texture, through its availability fence.
    // Write the texture's pixels only while it is available; rotate several textures to avoid
    // waiting. It does not synchronize other threads writing the same texture.
    [[nodiscard]] bool available() const;
    [[nodiscard]] const wil::com_ptr<ID3D11Texture2D>& texture() const noexcept { return texture_; }
    [[nodiscard]] const composition::CompositionTexture& surface() const noexcept { return surface_; }

private:
    wil::com_ptr<ID3D11Texture2D> texture_;
    composition::CompositionTexture surface_{nullptr};
};

}
