#include "VirtualDemoWindow.hpp"
#include <composia/TextLayout.hpp>
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <format>

using namespace composia;

namespace {
constexpr LONG extent = 1 << 20;
constexpr float header = 144, footer = 58;
}

VirtualDemoWindow::VirtualDemoWindow(Application& app)
    : Window(app, L"Composia | Virtual atlas", 1060, 760), app_(app),
      target_(app.compositor(), app.graphics(), hwnd()), document_(app.graphics(), {extent, extent}),
      home_(*this, L"Home"), far_(*this, L"Far corner"), minus_(*this, L"Zoom out"), plus_(*this, L"Zoom in") {
    brush_ = app.compositor().CreateSurfaceBrush(document_.surface());
    brush_.Stretch(composition::CompositionStretch::None);
    brush_.HorizontalAlignmentRatio(0);
    brush_.VerticalAlignmentRatio(0);
    map_ = app.compositor().CreateSpriteVisual();
    map_.Brush(brush_);
    map_.Offset({0, header, 0});
    map_.Clip(app.compositor().CreateInsetClip());
    target_.root().Children().InsertAtBottom(map_);
    clicks_[0] = home_.on_click([this] { origin_ = {}; invalidate(); });
    clicks_[1] = far_.on_click([this] { origin_ = {static_cast<float>(extent), static_cast<float>(extent)}; invalidate(); });
    clicks_[2] = minus_.on_click([this] { set_zoom(zoom_ / 1.25f, viewport_ * 0.5f); });
    clicks_[3] = plus_.on_click([this] { set_zoom(zoom_ * 1.25f, viewport_ * 0.5f); });
    SetWindowLongPtrW(hwnd(), GWL_STYLE, GetWindowLongPtrW(hwnd(), GWL_STYLE) | WS_HSCROLL | WS_VSCROLL);
    THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(hwnd(), nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED));
}

void VirtualDemoWindow::set_zoom(float value, numerics::float2 anchor) {
    const auto minimum = std::max({0.25f, viewport_.x / (12 * 512.0f), viewport_.y / (12 * 512.0f)});
    const auto next = std::clamp(value, minimum, std::max(minimum, 4.0f));
    origin_ += anchor / zoom_ - anchor / next;
    zoom_ = next;
    invalidate();
}

void VirtualDemoWindow::on_paint() {
    const auto pixels = client_pixels();
    if (pixels.cx <= 0 || pixels.cy <= 0 || IsIconic(hwnd())) { return; }
    app_.render([&] {
        target_.resize(pixels, dpi());
        const auto size = target_.logical_size();
        viewport_ = {size.x, std::max(1.0f, size.y - header - footer)};
        const auto minimum = std::max({0.25f, viewport_.x / (12 * 512.0f), viewport_.y / (12 * 512.0f)});
        zoom_ = std::max(zoom_, minimum);
        origin_.x = std::clamp(origin_.x, 0.0f, std::max(0.0f, extent - viewport_.x / zoom_));
        origin_.y = std::clamp(origin_.y, 0.0f, std::max(0.0f, extent - viewport_.y / zoom_));
        const RECT visible{static_cast<LONG>(std::floor(origin_.x)), static_cast<LONG>(std::floor(origin_.y)),
            std::min(extent, static_cast<LONG>(std::ceil(origin_.x + viewport_.x / zoom_))),
            std::min(extent, static_cast<LONG>(std::ceil(origin_.y + viewport_.y / zoom_)))};
        document_.update(visible, [this](ScopedSurfaceDraw& draw, const RECT& tile) { draw_tile(draw, tile); });
        map_.Size(viewport_);
        brush_.Scale({zoom_, zoom_});
        brush_.Offset(-origin_ * zoom_);
        for (const bool horizontal : {false, true}) {
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
            info.nMax = extent - 1;
            info.nPage = static_cast<UINT>(std::ceil((horizontal ? viewport_.x : viewport_.y) / zoom_));
            info.nPos = static_cast<int>(horizontal ? origin_.x : origin_.y);
            SetScrollInfo(hwnd(), horizontal ? SB_HORZ : SB_VERT, &info, TRUE);
        }
        home_.set_bounds({24, 91, 94, 36});
        far_.set_bounds({128, 91, 126, 36});
        minus_.set_bounds({264, 91, 112, 36});
        plus_.set_bounds({386, 91, 104, 36});
        draw_overlay();
    });
}

void VirtualDemoWindow::draw_tile(ScopedSurfaceDraw& draw, const RECT& tile) {
    const auto dc = draw.context().get();
    const auto column = tile.left / 512, row = tile.top / 512;
    const auto seed = static_cast<unsigned>(column * 37 + row * 73);
    dc->Clear(D2D1::ColorF(0xE3EADC));
    wil::com_ptr<ID2D1SolidColorBrush> ink;
    THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0xCFDDC5), ink.put()));
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float px = static_cast<float>(x * 128), py = static_cast<float>(y * 128);
            ink->SetColor(D2D1::ColorF(((seed + x + y * 3) % 5 == 0) ? 0xB3CFAD : 0xCFDDC5));
            dc->FillRoundedRectangle(D2D1::RoundedRect({px + 14, py + 14, px + 113, py + 113}, 10, 10), ink.get());
            ink->SetColor(D2D1::ColorF(0xB6C6B6));
            dc->FillRectangle({px + 32, py + 32, px + 59, py + 68}, ink.get());
            dc->FillRectangle({px + 72, py + 52, px + 95, py + 91}, ink.get());
        }
    }
    const auto river = [&](float x) {
        return D2D1_POINT_2F{x, 256 + 82 * std::sin((static_cast<float>(tile.left) + x) / 512)};
    };
    wil::com_ptr<ID2D1PathGeometry> path;
    wil::com_ptr<ID2D1GeometrySink> sink;
    THROW_IF_FAILED(app_.graphics().d2d_factory()->CreatePathGeometry(path.put()));
    THROW_IF_FAILED(path->Open(sink.put()));
    sink->BeginFigure(river(-16), D2D1_FIGURE_BEGIN_HOLLOW);
    for (float x = -8; x <= 528; x += 8) { sink->AddLine(river(x)); }
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    THROW_IF_FAILED(sink->Close());
    for (const bool bank : {true, false}) {
        ink->SetColor(D2D1::ColorF(bank ? 0x78BBC5 : 0x9DD3D8));
        dc->DrawGeometry(path.get(), ink.get(), bank ? 36.0f : 24.0f);
    }
    ink->SetColor(D2D1::ColorF(0xFFFFFF));
    for (float n = 0; n <= 512; n += 128) {
        dc->DrawLine({n, 0}, {n, 512}, ink.get(), 9);
        dc->DrawLine({0, n}, {512, n}, ink.get(), 9);
    }
    ink->SetColor(D2D1::ColorF(0xE1BB72));
    dc->DrawLine({0, 128}, {512, 128}, ink.get(), 3);
    dc->DrawLine({384, 0}, {384, 512}, ink.get(), 3);
    ink->SetColor(D2D1::ColorF(0xF9FCF7));
    dc->FillRoundedRectangle(D2D1::RoundedRect({26, 397, 355, 487}, 12, 12), ink.get());
    TextLayout name{draw.text_factory().get(), std::format(L"District {:04} / {:04}", column, row), 22, DWRITE_FONT_WEIGHT_SEMI_BOLD};
    name.resize(310, 32);
    ink->SetColor(D2D1::ColorF(0x264C48));
    dc->DrawTextLayout({42, 410}, name.layout().get(), ink.get());
    TextLayout coords{draw.text_factory().get(), std::format(L"x {:L}   y {:L}", tile.left, tile.top), 16};
    coords.resize(310, 28);
    dc->DrawTextLayout({42, 450}, coords.layout().get(), ink.get());
    ++painted_;
}

void VirtualDemoWindow::draw_overlay() {
    ScopedSurfaceDraw draw{target_.surface(), app_.graphics(), dpi()};
    const auto dc = draw.context().get();
    const auto size = target_.logical_size();
    dc->Clear(D2D1::ColorF(0, 0.0f));
    wil::com_ptr<ID2D1SolidColorBrush> ink;
    THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0x111E25), ink.put()));
    dc->FillRectangle({0, 0, size.x, header}, ink.get());
    dc->FillRectangle({0, size.y - footer, size.x, size.y}, ink.get());
    const auto text = [&](std::wstring_view value, float x, float y, float font, UINT32 color) {
        TextLayout label{draw.text_factory().get(), value, font};
        label.resize(std::max(1.0f, size.x - x - 20), 32);
        ink->SetColor(D2D1::ColorF(color));
        dc->DrawTextLayout({x, y}, label.layout().get(), ink.get());
    };
    text(L"Virtual atlas", 24, 16, 28, 0xECF5F3);
    text(L"1,048,576 × 1,048,576 pixels  /  4 TiB if fully allocated", 24, 55, 15, 0x8CB6AF);
    text(std::format(L"{:.0f}%   |   {} cached tiles   |   {:.1f} MiB retained pixels*   |   {} tile draws",
        zoom_ * 100, document_.cached_tiles(), document_.retained_pixel_bytes() / 1048576.0, painted_),
        24, size.y - 51, 15, 0xECF5F3);
    text(size.x < 700 ? L"Drag · Wheel · Ctrl+wheel zoom    *Not measured GPU memory" :
        L"Drag to pan · Wheel to scroll · Ctrl+wheel to zoom · Home / End   *Estimate, not measured GPU memory",
        24, size.y - 27, 12, 0x8CB6AF);
    draw.finish();
}

void VirtualDemoWindow::scroll(bool horizontal, UINT command) {
    auto& position = horizontal ? origin_.x : origin_.y;
    const auto page = (horizontal ? viewport_.x : viewport_.y) / zoom_;
    switch (command) {
    case SB_LINEUP: position -= 48 / zoom_; break;
    case SB_LINEDOWN: position += 48 / zoom_; break;
    case SB_PAGEUP: position -= page; break;
    case SB_PAGEDOWN: position += page; break;
    case SB_TOP: position = 0; break;
    case SB_BOTTOM: position = static_cast<float>(extent); break;
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION: {
        SCROLLINFO info{};
        info.cbSize = sizeof(info);
        info.fMask = SIF_TRACKPOS;
        GetScrollInfo(hwnd(), horizontal ? SB_HORZ : SB_VERT, &info);
        position = static_cast<float>(info.nTrackPos);
        break;
    }
    default: return;
    }
    invalidate();
}

std::optional<LRESULT> VirtualDemoWindow::on_message(UINT message, WPARAM wparam, LPARAM lparam) {
    const auto scale = static_cast<float>(dpi() ? dpi() : 96) / 96;
    switch (message) {
    case WM_GETMINMAXINFO: {
        auto info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = {static_cast<LONG>(560 * scale), static_cast<LONG>(420 * scale)};
        return 0;
    }
    case WM_HSCROLL: case WM_VSCROLL: scroll(message == WM_HSCROLL, LOWORD(wparam)); return 0;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL: {
        const auto steps = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
        if (GET_KEYSTATE_WPARAM(wparam) & MK_CONTROL) {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd(), &point);
            set_zoom(zoom_ * std::pow(1.25f, steps), {std::clamp(point.x / scale, 0.0f, viewport_.x),
                std::clamp(point.y / scale - header, 0.0f, viewport_.y)});
        } else if (message == WM_MOUSEHWHEEL || (GET_KEYSTATE_WPARAM(wparam) & MK_SHIFT)) {
            origin_.x += (message == WM_MOUSEHWHEEL ? 1 : -1) * steps * 96 / zoom_;
        } else { origin_.y -= steps * 96 / zoom_; }
        invalidate();
        return 0;
    }
    case WM_LBUTTONDOWN:
        if (GET_Y_LPARAM(lparam) / scale >= header && GET_Y_LPARAM(lparam) / scale < header + viewport_.y) {
            SetFocus(hwnd()); SetCapture(hwnd()); dragging_ = true;
            drag_ = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        }
        return 0;
    case WM_MOUSEMOVE:
        if (dragging_) {
            POINT next{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            origin_ += numerics::float2{static_cast<float>(drag_.x - next.x), static_cast<float>(drag_.y - next.y)} / (scale * zoom_);
            drag_ = next; invalidate();
        }
        return 0;
    case WM_LBUTTONUP: if (dragging_) { dragging_ = false; ReleaseCapture(); } return 0;
    case WM_CAPTURECHANGED: dragging_ = false; return 0;
    case WM_CANCELMODE: if (dragging_) { dragging_ = false; ReleaseCapture(); } return 0;
    case WM_KEYDOWN:
        switch (wparam) {
        case VK_HOME: origin_ = {}; break;
        case VK_END: origin_ = {static_cast<float>(extent), static_cast<float>(extent)}; break;
        case VK_LEFT: scroll(true, SB_LINEUP); break;
        case VK_RIGHT: scroll(true, SB_LINEDOWN); break;
        case VK_UP: scroll(false, SB_LINEUP); break;
        case VK_DOWN: scroll(false, SB_LINEDOWN); break;
        case VK_PRIOR: scroll(false, SB_PAGEUP); break;
        case VK_NEXT: scroll(false, SB_PAGEDOWN); break;
        case VK_ADD: case VK_OEM_PLUS: set_zoom(zoom_ * 1.25f, viewport_ * 0.5f); break;
        case VK_SUBTRACT: case VK_OEM_MINUS: set_zoom(zoom_ / 1.25f, viewport_ * 0.5f); break;
        default: return std::nullopt;
        }
        invalidate(); return 0;
    }
    return std::nullopt;
}
