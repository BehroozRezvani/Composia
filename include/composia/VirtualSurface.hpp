#pragma once

#include <composia/ScopedSurfaceDraw.hpp>
#include <functional>
#include <unordered_set>

namespace composia {

class VirtualSurface {
public:
    using Painter = std::function<void(ScopedSurfaceDraw&, const RECT& tile)>;
    static constexpr LONG maximum_dimension = 1 << 24;

    VirtualSurface(const GraphicsDevice&, SIZE pixels, LONG tileSize = 512, std::size_t maximumTiles = 256);
    VirtualSurface(const VirtualSurface&) = delete;
    VirtualSurface& operator=(const VirtualSurface&) = delete;

    // The callback draws in tile-local pixels at 96 DPI; tile is in document pixels.
    void update(RECT viewport, const Painter&);
    void invalidate(RECT dirty);
    void clear();
    void resize(SIZE pixels);
    [[nodiscard]] const composition::CompositionVirtualDrawingSurface& surface() const noexcept { return surface_; }
    [[nodiscard]] SIZE size() const noexcept { return size_; }
    [[nodiscard]] RECT retained_region() const noexcept { return retained_; }
    [[nodiscard]] std::size_t cached_tiles() const noexcept { return tiles_.size(); }
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
