#include <composia/ScreenCapture.hpp>
#include <d3d11_4.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <utility>

namespace composia {
namespace {
constexpr auto format = winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized;
constexpr int buffers = 2;

auto wrap_device(ID3D11Device* device) {
    THROW_HR_IF(E_INVALIDARG, !device);
    wil::com_ptr<IDXGIDevice> dxgi;
    THROW_IF_FAILED(device->QueryInterface(IID_PPV_ARGS(dxgi.put())));
    wil::com_ptr<ID3D11Multithread> multithread;
    THROW_IF_FAILED(device->QueryInterface(IID_PPV_ARGS(multithread.put())));
    multithread->SetMultithreadProtected(TRUE);
    winrt::com_ptr<IInspectable> inspectable;
    THROW_IF_FAILED(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
    return inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
}
}

bool ScreenCapture::supported() {
    const auto factory = winrt::try_get_activation_factory<capture::GraphicsCaptureSession, capture::IGraphicsCaptureSessionStatics>();
    return factory && factory.IsSupported();
}

void ScreenCapture::start(const capture::GraphicsCaptureItem& item, ID3D11Device* device) {
    THROW_HR_IF(E_INVALIDARG, !item || !device);
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), !supported());
    const auto target = item;
    close();
    try {
        item_ = target;
        closed_ = std::make_shared<std::atomic_bool>(false);
        closedEvent_ = item_.Closed(winrt::auto_revoke, [closed = closed_](const auto&, const auto&) noexcept { closed->store(true); });
        size_ = item_.Size();
        THROW_HR_IF(E_INVALIDARG, size_.Width <= 0 || size_.Height <= 0);
        device_ = wrap_device(device);
        pool_ = capture::Direct3D11CaptureFramePool::CreateFreeThreaded(device_, format, buffers, size_);
        session_ = pool_.CreateCaptureSession(item_);
        session_.StartCapture();
    } catch (...) { close(); throw; }
}

void ScreenCapture::recreate(ID3D11Device* device) {
    THROW_HR_IF(RO_E_CLOSED, !active());
    // Replacing only the pool can still return textures from the old D3D device.
    start(item_, device);
}

capture::Direct3D11CaptureFrame ScreenCapture::next_frame() {
    if (!active() || target_closed()) { return nullptr; }
    capture::Direct3D11CaptureFrame latest{nullptr};
    const auto release = wil::scope_exit([&] { if (latest) { latest.Close(); } });
    for (int index = 0; index < buffers; ++index) {
        auto frame = pool_.TryGetNextFrame();
        if (!frame) { break; }
        if (latest) { latest.Close(); }
        latest = std::move(frame);
    }
    if (!latest) { return nullptr; }
    const auto size = latest.ContentSize();
    if (size.Width <= 0 || size.Height <= 0) { return nullptr; }
    const auto surface = latest.Surface().Description();
    if (size.Width != size_.Width || size.Height != size_.Height || size.Width > surface.Width || size.Height > surface.Height) {
        latest.Close();
        latest = nullptr;
        pool_.Recreate(device_, format, buffers, size);
        size_ = size;
        return nullptr;
    }
    return std::exchange(latest, nullptr);
}

void ScreenCapture::close() noexcept {
    closedEvent_.revoke();
    auto session = std::exchange(session_, nullptr);
    auto pool = std::exchange(pool_, nullptr);
    try { if (session) { session.Close(); } }
    catch (...) { OutputDebugStringW(L"Composia capture session shutdown failed.\n"); }
    try { if (pool) { pool.Close(); } }
    catch (...) { OutputDebugStringW(L"Composia capture frame pool shutdown failed.\n"); }
    item_ = nullptr;
    device_ = nullptr;
    closed_.reset();
    size_ = {};
}

}
