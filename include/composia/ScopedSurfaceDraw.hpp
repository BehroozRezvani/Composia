#pragma once

#include <composia/GraphicsDevice.hpp>
#include <windows.ui.composition.interop.h>
#include <utility>
#include <vector>

namespace composia {

class ScopedSurfaceDraw {
public:
    ScopedSurfaceDraw(const composition::CompositionDrawingSurface&, const GraphicsDevice&, UINT dpi);
    ScopedSurfaceDraw(const composition::CompositionDrawingSurface&, const GraphicsDevice&, UINT dpi, const RECT& update);
    ~ScopedSurfaceDraw() noexcept;
    ScopedSurfaceDraw(const ScopedSurfaceDraw&) = delete;
    ScopedSurfaceDraw& operator=(const ScopedSurfaceDraw&) = delete;

    void finish();
    [[nodiscard]] const wil::com_ptr<ID2D1DeviceContext6>& context() const noexcept { return context_; }
    [[nodiscard]] const wil::com_ptr<IDWriteFactory7>& text_factory() const noexcept { return textFactory_; }
    [[nodiscard]] const wil::com_ptr<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>& interop() const noexcept { return interop_; }
    [[nodiscard]] POINT update_offset() const noexcept { return offset_; }
    // A solid brush owned by this scope, created once per color and valid until finish(). It
    // belongs to the current device, so nothing outlives a device replacement. Do not change its
    // color; ask for another brush instead.
    [[nodiscard]] ID2D1SolidColorBrush* solid_brush(const D2D1_COLOR_F& color);
    [[nodiscard]] ID2D1SolidColorBrush* solid_brush(UINT32 rgb, float alpha = 1.0f) { return solid_brush(D2D1::ColorF(rgb, alpha)); }

private:
    ScopedSurfaceDraw(const composition::CompositionDrawingSurface&, const GraphicsDevice&, UINT dpi, const RECT* update);
    wil::com_ptr<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop> interop_;
    wil::com_ptr<ID2D1DeviceContext6> context_;
    wil::com_ptr<IDWriteFactory7> textFactory_;
    std::vector<std::pair<D2D1_COLOR_F, wil::com_ptr<ID2D1SolidColorBrush>>> brushes_;
    POINT offset_{};
    bool drawing_{};
};

}
