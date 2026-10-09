#pragma once

#include <composia/Button.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <algorithm>
#include <cstdint>

// The demos' button look, given to every composia::Button they create: a rounded accent button on
// the demos' dark canvas, with a semibold label and a white focus outline. The canvas color fills
// the corners, since a child window cannot show its parent.
namespace demo {

inline void paint_button(composia::ScopedSurfaceDraw& draw, composia::numerics::float2 size, const composia::Button::State& state) {
    const auto dc = draw.context().get();
    dc->Clear(D2D1::ColorF(0x101923));
    const std::uint32_t fill = !state.enabled ? 0x35434B : state.pressed ? 0x3DAB93 : state.hovered ? 0xA2F5DF : 0x6FE6C8;
    const auto bounds = D2D1::RoundedRect({2, 2, std::max(2.0f, size.x - 2), std::max(2.0f, size.y - 2)}, 8, 8);
    dc->FillRoundedRectangle(bounds, draw.solid_brush(fill));
    if (state.focused) { dc->DrawRoundedRectangle(bounds, draw.solid_brush(0xFFFFFF), 2); }
    const auto label = state.label.layout().get();
    const DWRITE_TEXT_RANGE all{0, UINT32_MAX};
    label->SetFontSize(15, all);
    label->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, all);
    dc->DrawTextLayout({0, 0}, label, draw.solid_brush(state.enabled ? 0x101923 : 0x93A9B5));
}

}
