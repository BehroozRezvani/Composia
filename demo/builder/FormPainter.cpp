#include "FormPainter.hpp"
#include <algorithm>

namespace builder {
using composia::layout::Rect;
using namespace palette;

composia::TextLayout FormPainter::make(std::wstring_view value, float width, float height, const TextStyle& style) const {
    composia::TextLayout label{factory_, value, style.size, style.weight};
    label.resize(std::max(1.0f, width), std::max(1.0f, height));
    const auto layout = label.layout().get();
    THROW_IF_FAILED(layout->SetTextAlignment(style.align));
    THROW_IF_FAILED(layout->SetParagraphAlignment(style.middle ? DWRITE_PARAGRAPH_ALIGNMENT_CENTER : DWRITE_PARAGRAPH_ALIGNMENT_NEAR));
    if (!style.wrap) {
        THROW_IF_FAILED(layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
        wil::com_ptr<IDWriteInlineObject> ellipsis;
        THROW_IF_FAILED(factory_->CreateEllipsisTrimmingSign(layout, ellipsis.put()));
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        THROW_IF_FAILED(layout->SetTrimming(&trimming, ellipsis.get()));
    }
    return label;
}

float FormPainter::text(std::wstring_view value, Rect bounds, UINT32 color, const TextStyle& style) const {
    if (value.empty() || bounds.width <= 0 || bounds.height <= 0) { return 0; }
    const auto label = make(value, bounds.width, bounds.height, style);
    brush_->SetColor(D2D1::ColorF(color));
    dc_->DrawTextLayout({bounds.x, bounds.y}, label.layout().get(), brush_, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    DWRITE_TEXT_METRICS metrics{};
    THROW_IF_FAILED(label.layout()->GetMetrics(&metrics));
    return metrics.height;
}

float FormPainter::measure(std::wstring_view value, const TextStyle& style) const {
    if (value.empty()) { return 0; }
    DWRITE_TEXT_METRICS metrics{};
    THROW_IF_FAILED(make(value, 100000, 1000, style).layout()->GetMetrics(&metrics));
    return metrics.widthIncludingTrailingWhitespace;
}

void FormPainter::fill(Rect r, UINT32 color, float radius, float alpha) const {
    if (r.width <= 0 || r.height <= 0) { return; }
    brush_->SetColor(D2D1::ColorF(color, alpha));
    const D2D1_RECT_F rect{r.x, r.y, r.x + r.width, r.y + r.height};
    if (radius > 0) { dc_->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_); }
    else { dc_->FillRectangle(rect, brush_); }
}

void FormPainter::stroke(Rect r, UINT32 color, float width, float radius) const {
    if (r.width <= 0 || r.height <= 0) { return; }
    brush_->SetColor(D2D1::ColorF(color));
    const float half = width / 2;
    const D2D1_RECT_F rect{r.x + half, r.y + half, r.x + r.width - half, r.y + r.height - half};
    if (radius > 0) { dc_->DrawRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush_, width); }
    else { dc_->DrawRectangle(rect, brush_, width); }
}

void FormPainter::line(float x1, float y1, float x2, float y2, UINT32 color, float width) const {
    brush_->SetColor(D2D1::ColorF(color));
    dc_->DrawLine({x1, y1}, {x2, y2}, brush_, width);
}

void FormPainter::circle(float cx, float cy, float radius, UINT32 color, bool filled, float width) const {
    brush_->SetColor(D2D1::ColorF(color));
    if (filled) { dc_->FillEllipse(D2D1::Ellipse({cx, cy}, radius, radius), brush_); }
    else { dc_->DrawEllipse(D2D1::Ellipse({cx, cy}, radius, radius), brush_, width); }
}

void FormPainter::check(float x, float y, float size, UINT32 color, float width) const {
    line(x + size * 0.2f, y + size * 0.52f, x + size * 0.42f, y + size * 0.74f, color, width);
    line(x + size * 0.42f, y + size * 0.74f, x + size * 0.82f, y + size * 0.3f, color, width);
}

void FormPainter::cross(float x, float y, float size, UINT32 color, float width) const {
    line(x, y, x + size, y + size, color, width);
    line(x + size, y, x, y + size, color, width);
}

void FormPainter::glyph(Kind kind, float x, float y, float size, UINT32 color) const {
    const float s = size / 16;
    switch (kind) {
    case Kind::panel:
        stroke({x + 1 * s, y + 2 * s, 14 * s, 12 * s}, color, 1.2f, 2 * s);
        fill({x + 1 * s, y + 2 * s, 14 * s, 3.5f * s}, color, 1.5f * s);
        break;
    case Kind::label:
        text(L"Aa", {x, y - 1 * s, 16 * s, 18 * s}, color, {.size = 11.5f * s, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD, .align = DWRITE_TEXT_ALIGNMENT_CENTER, .middle = true});
        break;
    case Kind::button:
        fill({x + 1 * s, y + 4 * s, 14 * s, 8 * s}, color, 3 * s);
        break;
    case Kind::text_field:
        stroke({x + 1 * s, y + 4 * s, 14 * s, 8 * s}, color, 1.2f, 2 * s);
        line(x + 4.5f * s, y + 6 * s, x + 4.5f * s, y + 10 * s, color, 1.2f);
        break;
    case Kind::checkbox:
        stroke({x + 2 * s, y + 2 * s, 12 * s, 12 * s}, color, 1.2f, 2 * s);
        check(x + 2 * s, y + 2 * s, 12 * s, color, 1.5f);
        break;
    case Kind::slider:
        line(x + 1 * s, y + 8 * s, x + 15 * s, y + 8 * s, color, 1.6f);
        circle(x + 10 * s, y + 8 * s, 3 * s, color, true);
        break;
    case Kind::image:
        stroke({x + 1 * s, y + 2 * s, 14 * s, 12 * s}, color, 1.2f, 2 * s);
        line(x + 3 * s, y + 11.5f * s, x + 7 * s, y + 7 * s, color, 1.2f);
        line(x + 7 * s, y + 7 * s, x + 10 * s, y + 10 * s, color, 1.2f);
        line(x + 10 * s, y + 10 * s, x + 13 * s, y + 11.5f * s, color, 1.2f);
        circle(x + 11 * s, y + 5 * s, 1.3f * s, color, true);
        break;
    }
}

void FormPainter::clip(Rect r) const { dc_->PushAxisAlignedClip({r.x, r.y, r.x + r.width, r.y + r.height}, D2D1_ANTIALIAS_MODE_ALIASED); }
void FormPainter::unclip() const { dc_->PopAxisAlignedClip(); }

void FormPainter::scrollbar(Rect view, float extent, float scroll) const {
    if (extent <= view.height) { return; }
    const float length = std::max(24.0f, view.height * view.height / extent);
    const float top = view.y + (view.height - length) * scroll / (extent - view.height);
    fill({view.x + view.width - 6, top, 3, length}, thumb, 1.5f);
}

Rect intersect(Rect a, Rect b) noexcept {
    const float left = std::max(a.x, b.x), top = std::max(a.y, b.y);
    const float right = std::min(a.x + a.width, b.x + b.width), bottom = std::min(a.y + a.height, b.y + b.height);
    return {left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
}

Rect inset(Rect r, float amount) noexcept {
    return {r.x + amount, r.y + amount, std::max(0.0f, r.width - 2 * amount), std::max(0.0f, r.height - 2 * amount)};
}

Rect offset(Rect r, composia::numerics::float2 origin) noexcept { return {origin.x + r.x, origin.y + r.y, r.width, r.height}; }

Rect ancestor_clip(const Document& document, const std::vector<Placement>& placements, const Widget& widget, composia::numerics::float2 origin, Rect clip) noexcept {
    for (auto parent = widget.parent; parent != 0;) {
        const auto it = std::ranges::find(placements, parent, &Placement::id);
        const auto ancestor = document.find(parent);
        if (it == placements.end() || !ancestor) { break; }
        clip = intersect(clip, offset(it->bounds, origin));
        parent = ancestor->parent;
    }
    return clip;
}

void paint_widget(FormPainter& p, const Widget& widget, Rect r) {
    switch (widget.kind) {
    case Kind::panel:
        p.fill(r, panel, 8);
        p.stroke(r, border, 1, 8);
        p.text(widget.text, {r.x + 12, r.y + 8, std::max(1.0f, r.width - 24), 18}, inkMuted, {.size = 12, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD});
        break;
    case Kind::label:
        p.text(widget.text, r, ink, {.size = 14, .middle = true});
        break;
    case Kind::button:
        p.fill(inset(r, 2), accent, 8);
        p.text(widget.text, r, buttonInk, {.size = 15, .weight = DWRITE_FONT_WEIGHT_SEMI_BOLD, .align = DWRITE_TEXT_ALIGNMENT_CENTER, .middle = true});
        break;
    case Kind::text_field:
        p.fill(r, field, 6);
        p.stroke(r, border, 1, 6);
        p.text(widget.text, {r.x + 12, r.y, std::max(1.0f, r.width - 24), r.height}, inkFaint, {.size = 14, .middle = true});
        break;
    case Kind::checkbox: {
        const Rect box{r.x, r.y + (r.height - 18) / 2, 18, 18};
        if (widget.checked) {
            p.fill(box, accent, 4);
            p.check(box.x, box.y, 18, buttonInk, 2);
        } else {
            p.fill(box, field, 4);
            p.stroke(box, inkMuted, 1.2f, 4);
        }
        p.text(widget.text, {r.x + 28, r.y, std::max(1.0f, r.width - 28), r.height}, ink, {.size = 14, .middle = true});
        break;
    }
    case Kind::slider: {
        const float ty = r.y + r.height / 2, trackX = r.x + 8, trackW = std::max(1.0f, r.width - 16);
        const float ratio = static_cast<float>(std::clamp(widget.value, 0, 100)) / 100;
        p.fill({trackX, ty - 2, trackW, 4}, border, 2);
        p.fill({trackX, ty - 2, trackW * ratio, 4}, accent, 2);
        p.circle(trackX + trackW * ratio, ty, 8, accent, true);
        p.circle(trackX + trackW * ratio, ty, 3, buttonInk, true);
        break;
    }
    case Kind::image: {
        p.fill(r, chip, 6);
        p.stroke(r, border, 1, 6);
        const float g = std::clamp(std::min(r.width, r.height) * 0.5f, 16.0f, 48.0f);
        p.glyph(Kind::image, r.x + (r.width - g) / 2, r.y + (r.height - g) / 2 - (r.height > 60 ? 8 : 0), g, inkFaint);
        if (r.height > 60) { p.text(widget.text, {r.x + 4, r.y + r.height - 24, std::max(1.0f, r.width - 8), 18}, inkFaint, {.size = 11, .align = DWRITE_TEXT_ALIGNMENT_CENTER}); }
        break;
    }
    }
}

}
