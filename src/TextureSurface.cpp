#include <composia/TextureSurface.hpp>
#include <windows.ui.composition.interop.h>
#include <limits>

namespace composia {
namespace abi = ABI::Windows::UI::Composition;

bool TextureSurface::supported(const composition::Compositor& compositor, ID3D11Device* device) {
    THROW_HR_IF(E_INVALIDARG, !compositor || !device);
    const auto interop = compositor.try_as<abi::ICompositorInterop2>();
    if (!interop) { return false; }
    BOOL supported{};
    THROW_IF_FAILED(interop->CheckCompositionTextureSupport(device, &supported));
    return supported != FALSE;
}

TextureSurface::TextureSurface(const composition::Compositor& compositor, ID3D11Texture2D* texture,
    winrt::Windows::Graphics::DirectX::DirectXAlphaMode alpha) : texture_(texture) {
    THROW_HR_IF(E_INVALIDARG, !compositor || !texture);
    wil::com_ptr<ID3D11Device> device;
    texture->GetDevice(device.put());
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), !supported(compositor, device.get()));
    THROW_IF_FAILED(compositor.as<abi::ICompositorInterop2>()->CreateCompositionTexture(texture,
        reinterpret_cast<abi::ICompositionTexture**>(winrt::put_abi(surface_))));
    surface_.AlphaMode(alpha);
}

bool TextureSurface::available() const {
    UINT64 value{};
    wil::com_ptr<ID3D11Fence> fence;
    THROW_IF_FAILED(surface_.as<abi::ICompositionTextureInterop>()->GetAvailableFence(&value, IID_PPV_ARGS(fence.put())));
    if (!fence) { return false; }
    const auto completed = fence->GetCompletedValue();
    THROW_HR_IF(DXGI_ERROR_DEVICE_REMOVED, completed == std::numeric_limits<UINT64>::max());
    return completed >= value;
}

}
