#pragma once

#include <composia/Composition.hpp>
#include <chrono>

// Small shortcuts for building a visual tree. Composition runs every animation they start on its
// own, without the UI thread or a render loop.
namespace composia::animations {

// A container visual of the given size in DIPs.
[[nodiscard]] composition::ContainerVisual container(const composition::Compositor&, numerics::float2 size);
// A sprite visual of the given size in DIPs, filled with a solid color.
[[nodiscard]] composition::SpriteVisual sprite(const composition::Compositor&, numerics::float2 size, winrt::Windows::UI::Color);
// Animates every later change of the visual's Offset to its new value over the duration, instead
// of jumping there.
void implicit_offset(const composition::Visual&, std::chrono::milliseconds duration = std::chrono::milliseconds{300});
// Keeps the visual centered in the parent visual as either one changes size, through an
// expression animation on its Offset. The parent need not be the visual's parent in the tree.
void center_in_parent(const composition::Visual&, const composition::Visual& parent);

}
