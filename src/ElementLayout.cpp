#include <composia/Element.hpp>
#include <composia/ElementAccessibility.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace composia {
namespace {
using layout::Size;
constexpr float unbounded = std::numeric_limits<float>::infinity();

void validate(ElementLayout value) {
    if (!std::isfinite(value.gap) || value.gap < 0 || !value.columns ||
        (value.viewportLength && (!std::isfinite(*value.viewportLength) || *value.viewportLength < 0))) {
        throw std::invalid_argument("Element layout needs a nonnegative finite gap and at least one column");
    }
}
}

Element::Element(ElementLayout value) : layout_(value) { validate(value); }

void Element::set_layout(ElementLayout value) {
    check_mutation();
    validate(value);
    if (value.kind == ElementLayout::Kind::scroll && children_.size() > 1) {
        throw std::invalid_argument("A scroll element accepts one child");
    }
    if (layout_.kind == value.kind && layout_.gap == value.gap &&
        layout_.axis == value.axis && layout_.columns == value.columns && layout_.viewportLength == value.viewportLength) { return; }
    layout_ = value;
    orderDirty_ = true;
    scrollOffset_ = scrollLimit_ = 0;
    invalidate_measure();
}

Size Element::measure_content(Size) const { return preferred_; }

Size Element::measure(Size available) {
    if (!measureDirty_ && constraint_.width == available.width && constraint_.height == available.height) {
        return desired_;
    }
    measureDirty_ = true;
    const auto own = measure_content(available);
    if (!std::isfinite(own.width) || !std::isfinite(own.height) || own.width < 0 || own.height < 0) {
        throw std::invalid_argument("Element measurements must be finite and nonnegative");
    }
    const auto content = layout_children(available, false);
    desired_ = {std::min(available.width, std::max(own.width, content.width)),
                std::min(available.height, std::max(own.height, content.height))};
    if (!std::isfinite(desired_.width) || !std::isfinite(desired_.height)) {
        throw std::overflow_error("Element layout exceeds the DIP coordinate range");
    }
    constraint_ = available;
    measureDirty_ = false;
    arrangeDirty_ = true;
    return desired_;
}

void Element::arrange(layout::Rect box) {
    box.x += offset_.x;
    box.y += offset_.y;
    const bool resized = bounds_.width != box.width || bounds_.height != box.height;
    const bool notify = observeLayout_ && (arrangeDirty_ || resized || bounds_.x != box.x || bounds_.y != box.y);
    // A child can throw after this element has accepted its new bounds. Keep arrangement
    // pending until every child has received its slot, including on a same-size retry.
    if (resized) { arrangeDirty_ = true; }
    if (resized || bounds_.x != box.x || bounds_.y != box.y) {
        invalidate();
        bounds_ = box;
        invalidate();
    }
    if (arrangeDirty_) {
        layout_children({box.width, box.height}, true);
        arrangeDirty_ = false;
    }
    if (notify) { tree_->layoutCallbacks_.push_back(tree_->reference(this)); }
}

Size Element::layout_children(Size available, bool arranging) {
    if (children_.empty()) {
        scrollLimit_ = 0;
        if (arranging) { scrollOffset_ = 0; }
        return {};
    }
    using enum ElementLayout::Kind;
    const auto kind = layout_.kind;
    const float gap = layout_.gap;
    if (kind == overlay || kind == canvas) {
        Size result{};
        for (auto& child : children_) {
            const auto size = child->measure(available);
            result.width = std::max(result.width, size.width);
            result.height = std::max(result.height, size.height);
            if (arranging) {
                const auto slot = kind == canvas ? size : available;
                child->arrange({0, 0, slot.width, slot.height});
            }
        }
        return result;
    }

    if (kind == grid) {
        const auto columns = layout_.columns;
        const auto count = children_.size();
        float cellWidth{};
        if (std::isfinite(available.width)) {
            cellWidth = std::max(0.0f, available.width - static_cast<float>(columns - 1) * gap) / static_cast<float>(columns);
        } else {
            for (auto& child : children_) {
                cellWidth = std::max(cellWidth, child->measure({unbounded, unbounded}).width);
            }
        }
        float y{}, preferredWidth{};
        for (std::size_t first = 0; first < count;) {
            const auto end = first + std::min(columns, count - first);
            float height{};
            for (auto i = first; i < end; ++i) {
                const auto size = children_[i]->measure({cellWidth, unbounded});
                height = std::max(height, size.height);
                preferredWidth = std::max(preferredWidth, size.width);
            }
            if (arranging) {
                for (auto i = first; i < end; ++i) {
                    children_[i]->arrange({static_cast<float>(i - first) * (cellWidth + gap), y, cellWidth, height});
                }
            }
            y += height;
            if (end < count) { y += gap; }
            first = end;
        }
        return {static_cast<float>(columns) * preferredWidth + static_cast<float>(columns - 1) * gap, y};
    }

    const bool vertical = layout_.axis == layout::Axis::vertical;
    const auto main = [vertical](Size s) { return vertical ? s.height : s.width; };
    const auto cross = [vertical](Size s) { return vertical ? s.width : s.height; };
    const auto size = [vertical](float length, float breadth) {
        return vertical ? Size{breadth, length} : Size{length, breadth};
    };
    const auto place = [&](Element& child, float start, float length) {
        const auto s = size(length, cross(available));
        child.arrange({vertical ? 0.0f : start, vertical ? start : 0.0f, s.width, s.height});
    };
    const auto intrinsic = size(unbounded, cross(available));
    if (kind == scroll) {
        auto& child = *children_.front();
        const auto extent = child.measure(intrinsic);
        if (arranging) {
            scrollLimit_ = std::max(0.0f, main(extent) - main(available));
            scrollOffset_ = std::min(scrollOffset_, scrollLimit_);
            place(child, -scrollOffset_, std::max(main(extent), main(available)));
        }
        return layout_.viewportLength ? size(*layout_.viewportLength, cross(extent)) : extent;
    }

    const double gaps = static_cast<double>(children_.size() - 1) * gap;
    double bases{}, weights{};
    float breadth{};
    for (auto& child : children_) {
        // Flex children can be remeasured at their allocated length. The original basis
        // survives that second pass and is valid for arrangement at the same cross size.
        if (!(kind == flex && arranging && cross(available) == basisCross_)) {
            child->basis_ = main(child->measure(intrinsic));
        }
        bases += child->basis_;
        weights += child->grow_;
        breadth = std::max(breadth, cross(child->desired_));
    }
    basisCross_ = cross(available);
    const double room = std::max(0.0, static_cast<double>(main(available)) - gaps);
    const double free = room - bases;
    float start{};
    if (arranging || (kind == flex && std::isfinite(room))) {
        breadth = 0;
        for (auto& child : children_) {
            double length = child->basis_;
            if (kind == flex) {
                if (free < 0 && bases > 0) { length *= room / bases; }
                else if (weights > 0) { length += free * child->grow_ / weights; }
            }
            const float allocated = std::min(static_cast<float>(length), std::max(0.0f, main(available) - start));
            if (kind == flex) { child->measure(size(allocated, cross(available))); }
            breadth = std::max(breadth, cross(child->desired_));
            if (arranging) { place(*child, start, allocated); }
            start += allocated + gap;
        }
    }
    return size(static_cast<float>(bases + gaps), breadth);
}

}
