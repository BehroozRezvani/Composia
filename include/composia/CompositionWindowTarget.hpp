#pragma once

#include <composia/Composition.hpp>
#include <functional>

namespace composia {

class GraphicsDevice;
class ScopedSurfaceDraw;
class Window;

class CompositionWindowTarget {
public:
    CompositionWindowTarget(const composition::Compositor&, GraphicsDevice&, HWND);
    ~CompositionWindowTarget();
    CompositionWindowTarget(const CompositionWindowTarget&) = delete;
    CompositionWindowTarget& operator=(const CompositionWindowTarget&) = delete;

    void resize(SIZE pixels, UINT dpi);
    // Paints the window's client area in one step: sizes the surface to the client at the window's
    // DPI, opens a drawing scope through Application::render (which retries once after device
    // loss), and calls paint with the scope and the logical size in DIPs. Returns false without
    // painting when the client area is empty or the window is minimized.
    bool render(Window&, const std::function<void(ScopedSurfaceDraw&, numerics::float2 size)>& paint);
    [[nodiscard]] numerics::float2 logical_size() const noexcept { return logicalSize_; }
    [[nodiscard]] const composition::ContainerVisual& root() const noexcept { return root_; }
    [[nodiscard]] const composition::SpriteVisual& canvas() const noexcept { return canvas_; }
    [[nodiscard]] const composition::CompositionDrawingSurface& surface() const noexcept { return surface_; }
    [[nodiscard]] const composition::CompositionSurfaceBrush& brush() const noexcept { return brush_; }
    [[nodiscard]] const composition::Desktop::DesktopWindowTarget& target() const noexcept { return target_; }

private:
    composition::Desktop::DesktopWindowTarget target_{nullptr};
    composition::ContainerVisual root_{nullptr};
    composition::SpriteVisual canvas_{nullptr};
    composition::CompositionDrawingSurface surface_{nullptr};
    composition::CompositionSurfaceBrush brush_{nullptr};
    numerics::float2 logicalSize_{};
    SIZE pixels_{};
};

}
