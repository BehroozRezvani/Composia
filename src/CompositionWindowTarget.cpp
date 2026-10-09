#include <composia/CompositionWindowTarget.hpp>
#include <composia/Application.hpp>
#include <composia/GraphicsDevice.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <windows.ui.composition.interop.h>
#include <optional>

namespace composia {

CompositionWindowTarget::CompositionWindowTarget(const composition::Compositor& compositor,
    GraphicsDevice& graphics, HWND hwnd) : hwnd_(hwnd) {
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
        complete_ = false;  // Resizing discards the content.
    }
    if (dpi != dpi_) {
        dpi_ = dpi;
        complete_ = false;
    }
    const auto scale = static_cast<float>(dpi) / 96.0f;
    logicalSize_ = {static_cast<float>(pixels.cx) / scale, static_cast<float>(pixels.cy) / scale};
    root_.Size(logicalSize_);
    root_.Scale({scale, scale, 1.0f});
    canvas_.Size(logicalSize_);
}

bool CompositionWindowTarget::render(Window& window, const Painter& paint) {
    const auto pixels = window.client_pixels();
    THROW_HR_IF(E_INVALIDARG, window.hwnd() != hwnd_);
    if (pixels.cx <= 0 || pixels.cy <= 0 || IsIconic(window.hwnd())) {
        return false;
    }
    auto& application = window.application();
    const auto requested = window.paint_rect();
    bool painted{};
    application.render([&] {
        const auto dpi = window.dpi();
        resize(pixels, dpi);
        auto& graphics = application.graphics();
        std::optional<RECT> update;
        // Composition keeps the content outside an update, so a partial repaint is enough while
        // the surface holds a complete frame from the current device.
        if (requested && complete_ && generation_ == graphics.generation()) {
            const RECT whole{0, 0, pixels.cx, pixels.cy};
            RECT area{};
            if (!IntersectRect(&area, &*requested, &whole)) { return; }
            if (!EqualRect(&area, &whole)) { update = area; }
        }
        complete_ = false;
        std::optional<ScopedSurfaceDraw> draw;
        if (update) { draw.emplace(surface_, graphics, dpi, *update); }
        else { draw.emplace(surface_, graphics, dpi); }
        paint(*draw, logicalSize_);
        draw->finish();
        complete_ = true;
        generation_ = graphics.generation();
        painted = true;
    });
    return painted;
}

}
