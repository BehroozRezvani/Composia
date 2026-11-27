#pragma once

#include <span>
#include <vector>

// Geometry in device-independent pixels (DIPs), 1/96 inch; Window converts to and from pixels.
namespace composia::layout {

struct Point {
    float x{}, y{};
};

struct Size {
    float width{}, height{};
};

struct Rect {
    float x{}, y{}, width{}, height{};
    // True for points inside the left and top edges and before the right and bottom edges.
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && py >= y && px < x + width && py < y + height;
    }
};

// Space inside the edges of a box.
struct Insets {
    float left{}, top{}, right{}, bottom{};
    // The same space on every edge.
    [[nodiscard]] static constexpr Insets all(float value) noexcept { return {value, value, value, value}; }
    bool operator==(const Insets&) const = default;
};

enum class Axis { horizontal, vertical };

// Where children sit in a stack or flex container. Across its axis: stretched to fill it, or at
// their measured breadth at its start, centre or end. Along it: where the children sit as a
// group when they leave room, where stretch is the same as start.
enum class Align { stretch, start, center, end };

// Places items one after another along the axis inside bounds, each as long as requested and as
// wide as bounds across the axis, with gap between them. Items that do not fit are shortened, down
// to zero length at the far edge. Throws std::invalid_argument for non-finite or negative sizes.
[[nodiscard]] std::vector<Rect> stack(Rect bounds, std::span<const float> lengths,
    float gap = 8.0f, Axis axis = Axis::vertical);

}
