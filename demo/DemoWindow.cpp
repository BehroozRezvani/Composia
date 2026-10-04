#include "DemoWindow.hpp"
#include "SmokeTest.hpp"
#include <composia/AnimationHelpers.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <SimpleMath.h>
#include <algorithm>
#include <string_view>

using namespace composia;

DemoWindow::DemoWindow(Application& app, bool smokeTest)
    : Window(L"Composia", 960, 640), app_(app), target_(app.compositor(), app.graphics(), hwnd()) {
    stage_ = animations::container(app.compositor(), {160.0f, 160.0f});
    target_.root().Children().InsertAtTop(stage_);
    animations::center_in_parent(stage_, target_.root());

    tile_ = animations::sprite(app.compositor(), {128.0f, 128.0f}, {255, 111, 230, 200});
    auto rounded = app.compositor().CreateRoundedRectangleGeometry();
    rounded.Size(tile_.Size());
    rounded.CornerRadius({28.0f, 28.0f});
    tile_.Clip(app.compositor().CreateGeometricClip(rounded));
    stage_.Children().InsertAtTop(tile_);
    animations::bob(tile_, {16.0f, 24.0f, 0.0f}, 22.0f);

    indicator_ = animations::sprite(app.compositor(), {8.0f, 8.0f}, {255, 111, 230, 200});
    target_.root().Children().InsertAtTop(indicator_);
    animations::implicit_offset(indicator_);
    animations::pulse(indicator_);
    redraw();
    if (smokeTest) {
        smoke_ = std::make_unique<SmokeTest>(*this);
        THROW_LAST_ERROR_IF(SetTimer(hwnd(), 1, 300, nullptr) == 0);
    }
}

DemoWindow::~DemoWindow() { KillTimer(hwnd(), 1); }

void DemoWindow::redraw() {
    const auto pixels = client_pixels();
    if (pixels.cx <= 0 || pixels.cy <= 0 || IsIconic(hwnd())) {
        return;
    }
    app_.render([&] {
        target_.resize(pixels, dpi());
        const auto size = target_.logical_size();
        indicator_.Offset({size.x - 42.0f, 42.0f, 0.0f});
        draw_canvas();
    });
}

void DemoWindow::on_resize() { redraw(); }
void DemoWindow::on_paint() { redraw(); }

std::optional<LRESULT> DemoWindow::on_message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_TIMER && wparam == 1 && smoke_) {
        smoke_->tick();
        return 0;
    }
    if (message == WM_GETMINMAXINFO) {
        auto info = reinterpret_cast<MINMAXINFO*>(lparam);
        const auto windowDpi = hwnd() ? dpi() : GetDpiForSystem();
        info->ptMinTrackSize = {MulDiv(580, windowDpi, 96), MulDiv(460, windowDpi, 96)};
        return 0;
    }
    return std::nullopt;
}

void DemoWindow::draw_canvas() {
    ScopedSurfaceDraw draw(target_.surface(), app_.graphics(), dpi());
    const auto dc = draw.context().get();
    const auto size = target_.logical_size();
    dc->Clear(D2D1::ColorF(0x101923));

    wil::com_ptr<ID2D1SolidColorBrush> brush;
    THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0x24333F), brush.put()));
    for (float x = 32.0f; x < size.x; x += 32.0f) {
        for (float y = 120.0f; y < size.y - 88.0f; y += 32.0f) {
            dc->FillEllipse(D2D1::Ellipse({x, y}, 1.0f, 1.0f), brush.get());
        }
    }

    const auto text = [&](std::wstring_view value, float fontSize, D2D1_RECT_F rect,
                          UINT32 color, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL) {
        wil::com_ptr<IDWriteTextFormat> format;
        THROW_IF_FAILED(draw.text_factory()->CreateTextFormat(L"Segoe UI", nullptr, weight,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, fontSize, L"en-US", format.put()));
        brush->SetColor(D2D1::ColorF(color));
        dc->DrawTextW(value.data(), static_cast<UINT32>(value.size()), format.get(), rect, brush.get());
    };

    text(L"COMPOSIA", 14.0f, {32.0f, 31.0f, size.x - 64.0f, 55.0f}, 0x6FE6C8, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    text(L"A little motion. A native canvas.", 30.0f,
        {32.0f, 65.0f, size.x - 32.0f, 112.0f}, 0xEAF2F4, DWRITE_FONT_WEIGHT_SEMI_BOLD);

    // DirectXTK's math helpers are also available to framework consumers.
    const DirectX::SimpleMath::Vector2 center{size.x * 0.5f, size.y * 0.5f};
    brush->SetColor(D2D1::ColorF(0x314C57));
    dc->DrawEllipse(D2D1::Ellipse({center.x, center.y}, 142.0f, 142.0f), brush.get(), 1.0f);
    dc->DrawEllipse(D2D1::Ellipse({center.x, center.y}, 157.0f, 157.0f), brush.get(), 0.5f);

    brush->SetColor(D2D1::ColorF(0x31404A));
    dc->DrawLine({32.0f, size.y - 88.0f}, {size.x - 32.0f, size.y - 88.0f}, brush.get());
    text(L"Vector + text canvas", 17.0f, {32.0f, size.y - 69.0f, size.x - 32.0f, size.y - 44.0f}, 0xEAF2F4);
    text(L"Resize the window. The scene follows.", 14.0f,
        {32.0f, size.y - 41.0f, size.x - 32.0f, size.y - 16.0f}, 0x93A9B5);
    draw.finish();
    ++drawCount_;
}
