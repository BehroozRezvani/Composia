#pragma once

#include <composia/Composition.hpp>

namespace composia {

class GraphicsDevice;

class CompositionWindowTarget {
public:
    CompositionWindowTarget(const composition::Compositor&, GraphicsDevice&, HWND);
    ~CompositionWindowTarget();
    CompositionWindowTarget(const CompositionWindowTarget&) = delete;
    CompositionWindowTarget& operator=(const CompositionWindowTarget&) = delete;

    void resize(SIZE pixels, UINT dpi);
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
