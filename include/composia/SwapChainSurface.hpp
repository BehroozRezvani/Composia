#pragma once

#include <composia/Composition.hpp>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <functional>

namespace composia {

class Application;

// A DXGI swap chain made for Composition: Direct3D 11 output as a surface in the visual tree, where
// it is clipped, transformed, animated, and composed like any other visual. It works on Windows 10
// and later; TextureSurface needs Windows 11 composition textures. Put brush() on a SpriteVisual
// sized to the swap chain in DIPs.
//
// Nothing here runs a render loop. present renders one frame and queues it, and Composition keeps
// showing the last frame, so present only when the content changes: after input, new data, a
// resize, or a device replacement. Swap chains belong to the device that created them; after a
// graphics device replacement the swap chain is rebuilt, empty, on the next present or resize, and
// the window repaint that recovery triggers is the moment to present again.
class SwapChainSurface {
public:
    // Draws one frame into the back buffer. The view and the texture are valid only during the
    // call: release every reference to them, and anything created from them, before returning.
    using Renderer = std::function<void(ID3D11RenderTargetView* target, ID3D11Texture2D* buffer)>;

    // Opaque content uses DXGI_ALPHA_MODE_IGNORE; DXGI_ALPHA_MODE_PREMULTIPLIED composes with
    // what lies below. The Application must outlive the surface.
    SwapChainSurface(Application&, SIZE pixels, DXGI_ALPHA_MODE alpha = DXGI_ALPHA_MODE_IGNORE);
    SwapChainSurface(const SwapChainSurface&) = delete;
    SwapChainSurface& operator=(const SwapChainSurface&) = delete;

    // Changes the buffer size in pixels; the content is undefined until the next present.
    void resize(SIZE pixels);
    // Renders one frame and presents it, through Application::render, which retries once after
    // device loss. Afterwards no render target stays bound to the immediate context.
    void present(const Renderer& render);
    [[nodiscard]] SIZE size() const noexcept { return size_; }
    [[nodiscard]] const composition::CompositionSurfaceBrush& brush() const noexcept { return brush_; }
    // The native swap chain, for settings such as color space; resize through resize().
    [[nodiscard]] const wil::com_ptr<IDXGISwapChain1>& swap_chain() const noexcept { return swapChain_; }

private:
    void build();

    Application& application_;
    composition::CompositionSurfaceBrush brush_{nullptr};
    wil::com_ptr<IDXGISwapChain1> swapChain_;
    SIZE size_{};
    DXGI_ALPHA_MODE alpha_{};
    std::uint64_t generation_{};  // The graphics device generation the swap chain belongs to.
};

}
