#pragma once

#include "BuilderModel.hpp"
#include <composia/Composition.hpp>
#include <composia/TextLayout.hpp>
#include <d2d1_3.h>
#include <dwrite_3.h>
#include <vector>

namespace builder {

// Colors shared by the designer, its preview, and built apps.
namespace palette {
constexpr UINT32 chrome = 0x0B121B, pane = 0x101923, workspace = 0x0C131B, form = 0x131D28, panel = 0x18242F, divider = 0x1F2C38,
    border = 0x2A3946, ink = 0xEAF2F4, inkSoft = 0xC3D0D8, inkMuted = 0x93A9B5, inkFaint = 0x5F7380, accent = 0x6FE6C8, danger = 0xE66F8F,
    selected = 0x1C2B38, hover = 0x16212C, field = 0x0E161F, chip = 0x1C2733, gridDot = 0x1D2A36, buttonInk = 0x101923, thumb = 0x33424E,
    hoverOutline = 0x3B5160, shadow = 0x070C12;
}

struct TextStyle {
    float size = 14;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    bool wrap = false;
    DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING;
    bool middle = false;
};

// Drawing helpers for one surface update. Text layouts are rebuilt per draw.
class FormPainter {
public:
    FormPainter(ID2D1DeviceContext6* dc, IDWriteFactory7* factory, ID2D1SolidColorBrush* brush) : dc_(dc), factory_(factory), brush_(brush) {}

    [[nodiscard]] composia::TextLayout make(std::wstring_view value, float width, float height, const TextStyle&) const;
    float text(std::wstring_view value, composia::layout::Rect bounds, UINT32 color, const TextStyle& style = {}) const;
    [[nodiscard]] float measure(std::wstring_view value, const TextStyle& style = {}) const;
    void fill(composia::layout::Rect, UINT32 color, float radius = 0, float alpha = 1) const;
    void stroke(composia::layout::Rect, UINT32 color, float width = 1, float radius = 0) const;
    void line(float x1, float y1, float x2, float y2, UINT32 color, float width = 1) const;
    void circle(float cx, float cy, float radius, UINT32 color, bool filled, float width = 1) const;
    void check(float x, float y, float size, UINT32 color, float width = 1.8f) const;
    void cross(float x, float y, float size, UINT32 color, float width = 1.4f) const;
    void glyph(Kind, float x, float y, float size, UINT32 color) const;  // Vector pictograms on a 16 DIP grid.
    void clip(composia::layout::Rect) const;
    void unclip() const;
    void scrollbar(composia::layout::Rect view, float extent, float offset) const;

private:
    ID2D1DeviceContext6* dc_;
    IDWriteFactory7* factory_;
    ID2D1SolidColorBrush* brush_;
};

[[nodiscard]] composia::layout::Rect intersect(composia::layout::Rect, composia::layout::Rect) noexcept;
[[nodiscard]] composia::layout::Rect inset(composia::layout::Rect, float amount) noexcept;
[[nodiscard]] composia::layout::Rect offset(composia::layout::Rect, composia::numerics::float2 origin) noexcept;

// The region a widget may paint into: its ancestors' bounds (offset by origin) intersected with clip.
[[nodiscard]] composia::layout::Rect ancestor_clip(const Document&, const std::vector<Placement>&, const Widget&,
    composia::numerics::float2 origin, composia::layout::Rect clip) noexcept;

// Paints one widget's rendering into `bounds` (window DIPs). Buttons and text fields are drawn as
// stand-ins; a running form covers them with the framework's real controls.
void paint_widget(FormPainter&, const Widget&, composia::layout::Rect bounds);

}
