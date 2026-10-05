#include <composia/VirtualSurface.hpp>
#include <algorithm>
#include <array>

namespace composia {
namespace {
void validate_size(SIZE size) {
    THROW_HR_IF(E_INVALIDARG, size.cx <= 0 || size.cy <= 0 ||
        size.cx > VirtualSurface::maximum_dimension || size.cy > VirtualSurface::maximum_dimension);
}
std::uint64_t key(LONG x, LONG y) {
    return (static_cast<std::uint64_t>(y) << 32) | static_cast<std::uint32_t>(x);
}
}

VirtualSurface::VirtualSurface(const GraphicsDevice& graphics, SIZE pixels, LONG tileSize, std::size_t maximumTiles)
    : graphics_(graphics), size_(pixels), tileSize_(tileSize), maximumTiles_(maximumTiles), generation_(graphics.generation()) {
    validate_size(pixels);
    THROW_HR_IF(E_INVALIDARG, tileSize < 64 || tileSize > 2048 || maximumTiles == 0);
    surface_ = graphics.composition_device().CreateVirtualDrawingSurface({pixels.cx, pixels.cy},
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
}

void VirtualSurface::clear() {
    surface_.Trim({});
    tiles_.clear();
    retained_ = {};
    generation_ = graphics_.generation();
}

void VirtualSurface::resize(SIZE pixels) {
    validate_size(pixels);
    if (pixels.cx == size_.cx && pixels.cy == size_.cy) { return; }
    clear();
    surface_.Resize({pixels.cx, pixels.cy});
    size_ = pixels;
}

void VirtualSurface::invalidate(RECT dirty) {
    THROW_HR_IF(E_INVALIDARG, dirty.right < dirty.left || dirty.bottom < dirty.top);
    if (dirty.right == dirty.left || dirty.bottom == dirty.top) { return; }
    std::erase_if(tiles_, [&](std::uint64_t value) {
        const auto x = static_cast<LONG>(value & 0xffffffffu) * tileSize_;
        const auto y = static_cast<LONG>(value >> 32) * tileSize_;
        return x < dirty.right && y < dirty.bottom && x + tileSize_ > dirty.left && y + tileSize_ > dirty.top;
    });
}

std::uint64_t VirtualSurface::retained_pixel_bytes() const noexcept {
    return static_cast<std::uint64_t>(retained_.right - retained_.left) *
        static_cast<std::uint64_t>(retained_.bottom - retained_.top) * 4;
}

void VirtualSurface::update(RECT viewport, const Painter& paint) {
    THROW_HR_IF(E_INVALIDARG, !paint || viewport.right < viewport.left || viewport.bottom < viewport.top);
    viewport = {std::clamp(viewport.left, 0L, size_.cx), std::clamp(viewport.top, 0L, size_.cy),
        std::clamp(viewport.right, 0L, size_.cx), std::clamp(viewport.bottom, 0L, size_.cy)};
    if (viewport.left == viewport.right || viewport.top == viewport.bottom) { clear(); return; }
    const LONG left = std::max(0L, viewport.left / tileSize_ - 1);
    const LONG top = std::max(0L, viewport.top / tileSize_ - 1);
    const LONG right = std::min((size_.cx + tileSize_ - 1) / tileSize_, (viewport.right - 1) / tileSize_ + 2);
    const LONG bottom = std::min((size_.cy + tileSize_ - 1) / tileSize_, (viewport.bottom - 1) / tileSize_ + 2);
    const auto count = static_cast<std::uint64_t>(right - left) * static_cast<std::uint64_t>(bottom - top);
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY), count > maximumTiles_);
    if (generation_ != graphics_.generation()) { clear(); }
    const RECT region{left * tileSize_, top * tileSize_, std::min(size_.cx, right * tileSize_), std::min(size_.cy, bottom * tileSize_)};
    // Trim takes regions to retain. Trim before allocating the next viewport.
    const std::array regions{winrt::Windows::Graphics::RectInt32{region.left, region.top, region.right - region.left, region.bottom - region.top}};
    surface_.Trim(regions);
    retained_ = region;
    std::erase_if(tiles_, [&](std::uint64_t value) {
        const auto x = static_cast<LONG>(value & 0xffffffffu);
        const auto y = static_cast<LONG>(value >> 32);
        return x < left || x >= right || y < top || y >= bottom;
    });
    for (LONG y = top; y < bottom; ++y) {
        for (LONG x = left; x < right; ++x) {
            const auto value = key(x, y);
            if (tiles_.contains(value)) { continue; }
            const RECT tile{x * tileSize_, y * tileSize_, std::min(size_.cx, (x + 1) * tileSize_), std::min(size_.cy, (y + 1) * tileSize_)};
            ScopedSurfaceDraw draw{surface_, graphics_, 96, tile};
            const auto offset = draw.update_offset();
            draw.context()->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x), static_cast<float>(offset.y)));
            draw.context()->Clear(D2D1::ColorF(0, 0.0f));
            paint(draw, tile);
            draw.finish();
            tiles_.insert(value);
        }
    }
}

}
