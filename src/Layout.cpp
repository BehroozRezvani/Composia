#include <composia/Layout.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace composia::layout {

std::vector<Rect> stack(Rect bounds, std::span<const float> lengths, float gap, Axis axis) {
    const auto valid = [](float value) { return std::isfinite(value) && value >= 0; };
    if (!std::isfinite(bounds.x) || !std::isfinite(bounds.y) || !valid(bounds.width) || !valid(bounds.height) || !valid(gap)) {
        throw std::invalid_argument("Layout requires finite bounds and nonnegative dimensions");
    }
    std::vector<Rect> result;
    result.reserve(lengths.size());
    float offset{};
    const float available = axis == Axis::vertical ? bounds.height : bounds.width;
    for (auto length : lengths) {
        if (!valid(length)) { throw std::invalid_argument("Layout lengths must be finite and nonnegative"); }
        const auto size = std::min(length, std::max(0.0f, available - offset));
        result.push_back(axis == Axis::vertical
            ? Rect{bounds.x, bounds.y + offset, bounds.width, size}
            : Rect{bounds.x + offset, bounds.y, size, bounds.height});
        offset = std::min(available, offset + size + gap);
    }
    return result;
}

}
