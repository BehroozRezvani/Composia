#include <composia/CompositionWindowTarget.hpp>
#include <composia/Application.hpp>
#include <composia/GraphicsDevice.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <windows.ui.composition.interop.h>

namespace composia {

CompositionWindowTarget::CompositionWindowTarget(const composition::Compositor& compositor,
    GraphicsDevice& graphics, HWND hwnd) {
    THROW_IF_FAILED(compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>()
        ->CreateDesktopWindowTarget(hwnd, FALSE,
            reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(winrt::put_abi(target_))));
    root_ = compositor.CreateContainerVisual();
    target_.Root(root_);
    surface_ = graphics.composition_device().CreateDrawingSurface({1, 1},
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
    brush_ = compositor.CreateSurfaceBrush(surface_);
    brush_.Stretch(composition::CompositionStretch::Fill);
    canvas_ = compositor.CreateSpriteVisual();
    canvas_.Brush(brush_);
    root_.Children().InsertAtBottom(canvas_);
}

CompositionWindowTarget::~CompositionWindowTarget() {
    try {
        target_.Root(nullptr);
        target_.Close();
    } catch (...) {
        LOG_CAUGHT_EXCEPTION();
    }
}

void CompositionWindowTarget::resize(SIZE pixels, UINT dpi) {
    if (pixels.cx <= 0 || pixels.cy <= 0 || dpi == 0) {
        return;
    }
    if (pixels.cx != pixels_.cx || pixels.cy != pixels_.cy) {
        THROW_IF_FAILED(surface_.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>()->Resize(pixels));
        pixels_ = pixels;
    }
    const auto scale = static_cast<float>(dpi) / 96.0f;
    logicalSize_ = {static_cast<float>(pixels.cx) / scale, static_cast<float>(pixels.cy) / scale};
    root_.Size(logicalSize_);
    root_.Scale({scale, scale, 1.0f});
    canvas_.Size(logicalSize_);
}

bool CompositionWindowTarget::render(Window& window, const std::function<void(ScopedSurfaceDraw&, numerics::float2)>& paint) {
    const auto pixels = window.client_pixels();
    if (pixels.cx <= 0 || pixels.cy <= 0 || IsIconic(window.hwnd())) {
        return false;
    }
    auto& application = window.application();
    application.render([&] {
        const auto dpi = window.dpi();
        resize(pixels, dpi);
        ScopedSurfaceDraw draw{surface_, application.graphics(), dpi};
        paint(draw, logicalSize_);
        draw.finish();
    });
    return true;
}

}
