#include <composia/ScopedSurfaceDraw.hpp>

namespace composia {

ScopedSurfaceDraw::ScopedSurfaceDraw(const composition::CompositionDrawingSurface& surface,
    const GraphicsDevice& graphics, UINT dpi) : ScopedSurfaceDraw(surface, graphics, dpi, nullptr) {}

ScopedSurfaceDraw::ScopedSurfaceDraw(const composition::CompositionDrawingSurface& surface,
    const GraphicsDevice& graphics, UINT dpi, const RECT& update) : ScopedSurfaceDraw(surface, graphics, dpi, &update) {}

ScopedSurfaceDraw::ScopedSurfaceDraw(const composition::CompositionDrawingSurface& surface,
    const GraphicsDevice& graphics, UINT dpi, const RECT* update) : textFactory_(graphics.text_factory()) {
    THROW_HR_IF(E_INVALIDARG, dpi == 0);
    const auto native = surface.as<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
    interop_ = native.get();
    wil::com_ptr<ID2D1DeviceContext> baseContext;
    THROW_IF_FAILED(interop_->BeginDraw(update, __uuidof(ID2D1DeviceContext), baseContext.put_void(), &offset_));
    // A constructor failure after BeginDraw still has to close the surface update.
    const auto rollback = wil::scope_exit([&] { if (!drawing_) { end_clip(); LOG_IF_FAILED(interop_->EndDraw()); } });
    context_ = baseContext.query<ID2D1DeviceContext6>();
    const auto dotsPerInch = static_cast<float>(dpi);
    const auto dips = 96.0f / dotsPerInch;
    context_->SetDpi(dotsPerInch, dotsPerInch);
    context_->SetTransform(D2D1::Matrix3x2F::Translation(
        static_cast<float>(offset_.x - (update ? update->left : 0)) * dips,
        static_cast<float>(offset_.y - (update ? update->top : 0)) * dips));
    context_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (update) {
        // The update may share an atlas with other surfaces; nothing may be drawn outside it.
        bounds_ = {static_cast<float>(update->left) * dips, static_cast<float>(update->top) * dips,
            static_cast<float>(update->right) * dips, static_cast<float>(update->bottom) * dips};
        context_->PushAxisAlignedClip(bounds_, D2D1_ANTIALIAS_MODE_ALIASED);
        clipped_ = true;
    } else {
        const auto size = surface.Size();
        bounds_ = {0, 0, size.Width * dips, size.Height * dips};
    }
    drawing_ = true;
}

ScopedSurfaceDraw::~ScopedSurfaceDraw() noexcept {
    if (drawing_) {
        brushes_.clear();
        end_clip();
        const auto result = interop_->EndDraw();
        context_.reset();
        LOG_IF_FAILED(result);
    }
}

void ScopedSurfaceDraw::end_clip() noexcept {
    if (clipped_ && context_) {
        context_->PopAxisAlignedClip();
        clipped_ = false;
    }
}

void ScopedSurfaceDraw::finish() {
    if (drawing_) {
        drawing_ = false;
        brushes_.clear();
        end_clip();
        const auto result = interop_->EndDraw();
        context_.reset();
        THROW_IF_FAILED(result);
    }
}

ID2D1SolidColorBrush* ScopedSurfaceDraw::solid_brush(const D2D1_COLOR_F& color) {
    THROW_HR_IF(E_NOT_VALID_STATE, !context_);
    for (const auto& [existing, brush] : brushes_) {
        if (existing.r == color.r && existing.g == color.g && existing.b == color.b && existing.a == color.a) { return brush.get(); }
    }
    wil::com_ptr<ID2D1SolidColorBrush> brush;
    THROW_IF_FAILED(context_->CreateSolidColorBrush(color, brush.put()));
    brushes_.emplace_back(color, brush);
    return brush.get();
}

}
