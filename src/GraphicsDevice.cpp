#include <composia/GraphicsDevice.hpp>
#include <windows.ui.composition.interop.h>
#include <spdlog/spdlog.h>

namespace composia {
namespace abi = ABI::Windows::UI::Composition;

GraphicsDevice::GraphicsDevice(const composition::Compositor& compositor, bool forceWarp)
    : compositor_(compositor), forceWarp_(forceWarp) {
    removedEvent_.reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    THROW_LAST_ERROR_IF_NULL(removedEvent_);
    D2D1_FACTORY_OPTIONS options{};
    THROW_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        __uuidof(ID2D1Factory7), &options, d2dFactory_.put_void()));
    THROW_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory7),
        reinterpret_cast<IUnknown**>(textFactory_.put())));
    recreate();
}

GraphicsDevice::~GraphicsDevice() { unregister_device(); }

void GraphicsDevice::unregister_device() noexcept {
    if (registered_) {
        d3d_->UnregisterDeviceRemoved(removedCookie_);
        registered_ = false;
    }
}

void GraphicsDevice::recreate() {
    wil::com_ptr<ID3D11Device> baseDevice;
    wil::com_ptr<ID3D11DeviceContext> context;
    constexpr D3D_FEATURE_LEVEL levels[]{D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    auto create = [&](D3D_DRIVER_TYPE driver) {
        return D3D11CreateDevice(nullptr, driver, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, baseDevice.put(), nullptr,
            context.put());
    };
    auto driver = forceWarp_ ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE;
    auto result = create(driver);
    if (FAILED(result) && driver == D3D_DRIVER_TYPE_HARDWARE) {
        spdlog::warn("event=hardware_device_unavailable hresult=0x{:08X} fallback=warp", static_cast<unsigned>(result));
        baseDevice.reset();
        context.reset();
        driver = D3D_DRIVER_TYPE_WARP;
        result = create(driver);
    }
    THROW_IF_FAILED(result);
    auto device = baseDevice.query<ID3D11Device5>();
    auto dxgi = device.query<IDXGIDevice>();
    wil::com_ptr<ID2D1Device6> d2d;
    wil::com_ptr<ID2D1DeviceContext6> d2dContext;
    THROW_IF_FAILED(d2dFactory_->CreateDevice(dxgi.get(), d2d.put()));
    THROW_IF_FAILED(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.put()));

    if (compositionDevice_) {
        THROW_IF_FAILED(compositionDevice_.as<abi::ICompositionGraphicsDeviceInterop>()->SetRenderingDevice(d2d.get()));
    } else {
        THROW_IF_FAILED(compositor_.as<abi::ICompositorInterop>()->CreateGraphicsDevice(d2d.get(),
            reinterpret_cast<abi::ICompositionGraphicsDevice**>(wil::put_abi(compositionDevice_))));
    }

    unregister_device();
    THROW_IF_WIN32_BOOL_FALSE(ResetEvent(removedEvent_.get()));
    d3d_ = std::move(device);
    d3dContext_ = std::move(context);
    d2d_ = std::move(d2d);
    d2dContext_ = std::move(d2dContext);
    THROW_IF_FAILED(d3d_->RegisterDeviceRemovedEvent(removedEvent_.get(), &removedCookie_));
    registered_ = true;
    ++generation_;
    spdlog::info("event=graphics_device_created generation={} driver={}", generation_,
        driver == D3D_DRIVER_TYPE_WARP ? "warp" : "hardware");
}

bool GraphicsDevice::is_device_loss(HRESULT error) const noexcept {
    return error == DXGI_ERROR_DEVICE_REMOVED || error == DXGI_ERROR_DEVICE_RESET ||
        error == DXGI_ERROR_DEVICE_HUNG || error == D2DERR_RECREATE_TARGET ||
        (d3d_ && FAILED(d3d_->GetDeviceRemovedReason()));
}

}
