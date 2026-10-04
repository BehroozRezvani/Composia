#pragma once

#include <composia/Composition.hpp>
#include <chrono>

namespace composia::animations {

[[nodiscard]] composition::ContainerVisual container(const composition::Compositor&, numerics::float2 size);
[[nodiscard]] composition::SpriteVisual sprite(const composition::Compositor&, numerics::float2 size, winrt::Windows::UI::Color);
void implicit_offset(const composition::Visual&, std::chrono::milliseconds duration = std::chrono::milliseconds{300});
void bob(const composition::Visual&, numerics::float3 origin, float distance);
void pulse(const composition::Visual&);
void center_in_parent(const composition::Visual&, const composition::Visual& parent);

}
