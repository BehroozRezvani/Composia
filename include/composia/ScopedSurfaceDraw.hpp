#pragma once

#include <composia/GraphicsDevice.hpp>
#include <windows.ui.composition.interop.h>

namespace composia {

class ScopedSurfaceDraw {
public:
    ScopedSurfaceDraw(const composition::CompositionDrawingSurface&, const GraphicsDevice&, UINT dpi);
    ~ScopedSurfaceDraw() noexcept;
    ScopedSurfaceDraw(const ScopedSurfaceDraw&) = delete;
    ScopedSurfaceDraw& operator=(const ScopedSurfaceDraw&) = delete;

    void finish();
    [[nodiscard]] const wil::com_ptr<ID2D1DeviceContext6>& context() const noexcept { return context_; }
    [[nodiscard]] const wil::com_ptr<IDWriteFactory7>& text_factory() const noexcept { return textFactory_; }
    [[nodiscard]] const wil::com_ptr<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>& interop() const noexcept { return interop_; }
    [[nodiscard]] POINT update_offset() const noexcept { return offset_; }

private:
    wil::com_ptr<ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop> interop_;
    wil::com_ptr<ID2D1DeviceContext6> context_;
    wil::com_ptr<IDWriteFactory7> textFactory_;
    POINT offset_{};
    bool drawing_{};
};

}
