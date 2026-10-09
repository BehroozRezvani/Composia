#pragma once

#include <composia/GraphicsDevice.hpp>
#include <composia/TextureSurface.hpp>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <array>
#include <memory>

class TextureFrames {
public:
    struct Frame {
        std::unique_ptr<composia::TextureSurface> surface;
        wil::com_ptr<ID3D11RenderTargetView> target;
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface video{nullptr};
    };
    TextureFrames(const composia::composition::Compositor&, composia::GraphicsDevice&, SIZE);
    Frame* acquire();
    void present(Frame&, const composia::composition::CompositionSurfaceBrush&);
    [[nodiscard]] SIZE size() const noexcept { return size_; }
    [[nodiscard]] unsigned presented() const noexcept { return presented_; }
    [[nodiscard]] ID3D11Texture2D* current_texture() const noexcept { return current_ ? current_->surface->texture().get() : nullptr; }

private:
    std::array<Frame, 3> frames_;
    Frame* current_{};
    SIZE size_{};
    unsigned presented_{};
};
