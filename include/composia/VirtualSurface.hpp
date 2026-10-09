#pragma once

#include <composia/ScopedSurfaceDraw.hpp>
#include <cstdint>
#include <functional>
#include <unordered_set>

namespace composia {

// A sparse document surface far larger than any bitmap, such as a map, a page stack, or an image
// canvas, drawn in square tiles only where a viewport needs them. It keeps the tiles covering the
// latest viewport plus one tile of overscan, trims everything else from the surface, and repaints
// a tile only when it is missing, invalidated, or lost with the graphics device. Show surface()
// through a surface brush; sizes, viewports, and tiles are in document pixels, independent of DPI.
class VirtualSurface {
public:
    // Draws one tile. The context draws in tile-local pixels at 96 DPI, cleared to transparent;
    // tile is the tile's rectangle in document pixels. Do not reenter the surface from here.
    using Painter = std::function<void(ScopedSurfaceDraw&, const RECT& tile)>;
    // The largest width or height, in pixels.
    static constexpr LONG maximum_dimension = 1 << 24;

    // tileSize is 64 to 2048 pixels. maximumTiles bounds the tiles one viewport may retain; a
    // larger viewport is rejected instead of allocated. Keep the graphics device alive.
    VirtualSurface(const GraphicsDevice&, SIZE pixels, LONG tileSize = 512, std::size_t maximumTiles = 256);
    VirtualSurface(const VirtualSurface&) = delete;
    VirtualSurface& operator=(const VirtualSurface&) = delete;

    // Makes the viewport's tiles and their overscan present, painting those that are not, and
    // releases the rest. The viewport is clipped to the surface; an empty one releases every
    // tile. A viewport that needs more tiles than the budget throws
    // HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY) and changes nothing. Call it on the UI thread,
    // inside Application::render so a device loss is retried.
    void update(RECT viewport, const Painter&);
    // Marks the tiles intersecting the rectangle for repainting by the next update.
    void invalidate(RECT dirty);
    // Releases every tile.
    void clear();
    // Changes the document size, releasing every tile.
    void resize(SIZE pixels);
    [[nodiscard]] const composition::CompositionVirtualDrawingSurface& surface() const noexcept { return surface_; }
    [[nodiscard]] SIZE size() const noexcept { return size_; }
    // The document pixels the surface keeps: the latest viewport's tiles and overscan.
    [[nodiscard]] RECT retained_region() const noexcept { return retained_; }
    [[nodiscard]] std::size_t cached_tiles() const noexcept { return tiles_.size(); }
    // The retained region's size as BGRA bytes: an estimate of the content, not of GPU memory.
    [[nodiscard]] std::uint64_t retained_pixel_bytes() const noexcept;

private:
    const GraphicsDevice& graphics_;
    composition::CompositionVirtualDrawingSurface surface_{nullptr};
    SIZE size_{};
    LONG tileSize_{};
    std::size_t maximumTiles_{};
    std::uint64_t generation_{};
    RECT retained_{};
    std::unordered_set<std::uint64_t> tiles_;
};

}
