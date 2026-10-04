#include <composia/ScopedSurfaceDraw.hpp>

namespace composia {

ScopedSurfaceDraw::ScopedSurfaceDraw(const composition::CompositionDrawingSurface& surface,
    const GraphicsDevice& graphics, UINT dpi) : textFactory_(graphics.text_factory()) {
    THROW_HR_IF(E_INVALIDARG, dpi == 0);
    const auto native = surface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
    interop_ = native.get();
    wil::com_ptr<ID2D1DeviceContext> baseContext;
    THROW_IF_FAILED(interop_->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), baseContext.put_void(), &offset_));
    // A constructor failure after BeginDraw still has to close the surface update.
    const auto rollback = wil::scope_exit([&] { if (!drawing_) { LOG_IF_FAILED(interop_->EndDraw()); } });
    context_ = baseContext.query<ID2D1DeviceContext6>();
    const auto dotsPerInch = static_cast<float>(dpi);
    context_->SetDpi(dotsPerInch, dotsPerInch);
    context_->SetTransform(D2D1::Matrix3x2F::Translation(
        static_cast<float>(offset_.x) * 96.0f / dotsPerInch,
        static_cast<float>(offset_.y) * 96.0f / dotsPerInch));
    context_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    drawing_ = true;
}

ScopedSurfaceDraw::~ScopedSurfaceDraw() noexcept {
    if (drawing_) {
        const auto result = interop_->EndDraw();
        context_.reset();
        LOG_IF_FAILED(result);
    }
}

void ScopedSurfaceDraw::finish() {
    if (drawing_) {
        drawing_ = false;
        const auto result = interop_->EndDraw();
        context_.reset();
        THROW_IF_FAILED(result);
    }
}

}
