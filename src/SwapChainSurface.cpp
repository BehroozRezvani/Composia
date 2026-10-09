#include <composia/SwapChainSurface.hpp>
#include <composia/Application.hpp>
#include <windows.ui.composition.interop.h>

namespace composia {
namespace abi = ABI::Windows::UI::Composition;

SwapChainSurface::SwapChainSurface(Application& application, SIZE pixels, DXGI_ALPHA_MODE alpha)
    : application_(application), size_(pixels), alpha_(alpha) {
    THROW_HR_IF(E_INVALIDARG, pixels.cx <= 0 || pixels.cy <= 0 ||
        (alpha != DXGI_ALPHA_MODE_IGNORE && alpha != DXGI_ALPHA_MODE_PREMULTIPLIED));
    brush_ = application.compositor().CreateSurfaceBrush();
    build();
}

// Creates the swap chain on the current device and points the brush at it.
void SwapChainSurface::build() {
    auto& graphics = application_.graphics();
    const auto& device = graphics.d3d_device();
    wil::com_ptr<IDXGIAdapter> adapter;
    THROW_IF_FAILED(device.query<IDXGIDevice>()->GetAdapter(adapter.put()));
    wil::com_ptr<IDXGIFactory2> factory;
    THROW_IF_FAILED(adapter->GetParent(IID_PPV_ARGS(factory.put())));
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(size_.cx);
    desc.Height = static_cast<UINT>(size_.cy);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.Scaling = DXGI_SCALING_STRETCH;  // The only scaling Composition swap chains support.
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.AlphaMode = alpha_;
    wil::com_ptr<IDXGISwapChain1> swapChain;
    THROW_IF_FAILED(factory->CreateSwapChainForComposition(device.get(), &desc, nullptr, swapChain.put()));
    composition::ICompositionSurface surface{nullptr};
    THROW_IF_FAILED(application_.compositor().as<abi::ICompositorInterop>()->CreateCompositionSurfaceForSwapChain(
        swapChain.get(), reinterpret_cast<abi::ICompositionSurface**>(winrt::put_abi(surface))));
    brush_.Surface(surface);
    swapChain_ = std::move(swapChain);
    generation_ = graphics.generation();
}

void SwapChainSurface::resize(SIZE pixels) {
    THROW_HR_IF(E_INVALIDARG, pixels.cx <= 0 || pixels.cy <= 0);
    if (pixels.cx == size_.cx && pixels.cy == size_.cy) { return; }
    application_.render([&] {
        auto& graphics = application_.graphics();
        if (generation_ != graphics.generation()) {
            size_ = pixels;
            build();
            return;
        }
        // ResizeBuffers fails while anything still refers to the buffers, including bound views.
        const auto& context = graphics.d3d_context();
        context->OMSetRenderTargets(0, nullptr, nullptr);
        context->Flush();
        THROW_IF_FAILED(swapChain_->ResizeBuffers(0, static_cast<UINT>(pixels.cx), static_cast<UINT>(pixels.cy), DXGI_FORMAT_UNKNOWN, 0));
        size_ = pixels;
    });
}

void SwapChainSurface::present(const Renderer& render) {
    THROW_HR_IF(E_INVALIDARG, !render);
    application_.render([&] {
        auto& graphics = application_.graphics();
        if (generation_ != graphics.generation()) { build(); }
        const auto& context = graphics.d3d_context();
        const auto unbind = wil::scope_exit([&] { context->OMSetRenderTargets(0, nullptr, nullptr); });
        {
            wil::com_ptr<ID3D11Texture2D> buffer;
            THROW_IF_FAILED(swapChain_->GetBuffer(0, IID_PPV_ARGS(buffer.put())));
            wil::com_ptr<ID3D11RenderTargetView> target;
            THROW_IF_FAILED(graphics.d3d_device()->CreateRenderTargetView(buffer.get(), nullptr, target.put()));
            render(target.get(), buffer.get());
        }
        THROW_IF_FAILED(swapChain_->Present(1, 0));
    });
}

}
