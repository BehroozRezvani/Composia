#pragma once

#include <span>
#include <vector>

namespace composia::layout {

struct Rect {
    float x{}, y{}, width{}, height{};
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

enum class Axis { horizontal, vertical };
[[nodiscard]] std::vector<Rect> stack(Rect bounds, std::span<const float> lengths,
    float gap = 8.0f, Axis axis = Axis::vertical);

}
