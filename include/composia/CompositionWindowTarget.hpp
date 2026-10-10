#pragma once

#include <composia/Composition.hpp>
#include <cstdint>
#include <functional>
#include <memory>

namespace composia {

class GraphicsDevice;
class ScopedSurfaceDraw;
class Window;

// Puts Composition content in a window: a visual tree rooted at root(), scaled by the window's DPI
// so its children are laid out in DIPs, with a canvas sprite at the bottom that shows a drawing
// surface sized to the client area. Add visuals above the canvas through root().Children().
class CompositionWindowTarget {
public:
    enum class Hosting { standalone, shared };
    // Shared targets require Window::Content::composition. A top-level target hosts the tree;
    // a child joins its immediate parent's shared target, which must already exist. Position,
    // rectangular clipping, visibility and sibling order then travel with the painted content.
    // The Window and Application must outlive this target; child targets must go first. Native
    // controls remain independent. Reparenting and nonrectangular HWND regions are unsupported
    // and reported as errors. Use Application::update for layout, paint and commit together.
    CompositionWindowTarget(Window&, Hosting);
    [[nodiscard]] static bool hosts_children(HWND) noexcept;
    // Paints the canvas in DIPs; size is the client area in DIPs.
    using Painter = std::function<void(ScopedSurfaceDraw&, numerics::float2 size)>;

    // Attaches to the window's HWND, which must not already have a Composition target.
    CompositionWindowTarget(const composition::Compositor&, GraphicsDevice&, HWND);
    // Detaches the visual tree from the window.
    ~CompositionWindowTarget();
    CompositionWindowTarget(const CompositionWindowTarget&) = delete;
    CompositionWindowTarget& operator=(const CompositionWindowTarget&) = delete;

    // Sizes the surface in pixels and the visual tree in DIPs for the DPI; render calls it. A
    // size or DPI change discards the surface content. Empty sizes and a zero DPI are ignored.
    void resize(SIZE pixels, UINT dpi);
    // Paints the client area of the window this target was created for, in one step; any other
    // window is rejected with E_INVALIDARG. Sizes the surface to the client at the window's DPI,
    // opens a drawing scope through Application::render (which retries once after device loss),
    // and calls paint with the scope and the logical size in DIPs. Inside on_paint, when the
    // surface already holds a complete frame, only the area being repainted is updated: the
    // scope is clipped to it and ScopedSurfaceDraw::update_bounds() reports it. A resize, a DPI
    // change, or a replaced graphics device repaints everything. Returns false without painting
    // when the client area is empty, the window is minimized, or no visible area needs painting.
    bool render(Window&, const Painter& paint);
    // A fill under the canvas and children, including newly exposed space during a resize.
    void set_background(winrt::Windows::UI::Color);
    // The client area in DIPs as of the last resize.
    [[nodiscard]] numerics::float2 logical_size() const noexcept { return logicalSize_; }
    [[nodiscard]] const composition::ContainerVisual& root() const noexcept { return root_; }
    [[nodiscard]] const composition::SpriteVisual& canvas() const noexcept { return canvas_; }
    [[nodiscard]] const composition::CompositionDrawingSurface& surface() const noexcept { return surface_; }
    [[nodiscard]] const composition::CompositionSurfaceBrush& brush() const noexcept { return brush_; }
    [[nodiscard]] const composition::Desktop::DesktopWindowTarget& target() const noexcept { return target_; }

private:
    struct Host;
    std::unique_ptr<Host> host_;
    composition::ContainerVisual frame_{nullptr};
    composition::CompositionColorBrush background_{nullptr};
    composition::Desktop::DesktopWindowTarget target_{nullptr};
    composition::ContainerVisual root_{nullptr};
    composition::SpriteVisual canvas_{nullptr};
    composition::CompositionDrawingSurface surface_{nullptr};
    composition::CompositionSurfaceBrush brush_{nullptr};
    numerics::float2 logicalSize_{};
    SIZE pixels_{};
    HWND hwnd_{};
    UINT dpi_{};
    std::uint64_t generation_{};  // The graphics device generation of the last complete frame.
    bool complete_{};             // The surface holds a complete frame at its current size and DPI.
};

}
