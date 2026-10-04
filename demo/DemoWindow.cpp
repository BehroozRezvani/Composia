#include "DemoWindow.hpp"
#include <composia/AnimationHelpers.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <SimpleMath.h>
#include <algorithm>
#include <string_view>

using namespace composia;

DemoWindow::DemoWindow(Application& app)
    : Window(app, L"Composia", 960, 640), app_(app), target_(app.compositor(), app.graphics(), hwnd()) {
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
    const auto factory = app.graphics().text_factory().get();
    labels_.emplace_back(factory, L"COMPOSIA", 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    labels_.emplace_back(factory, L"A little motion. A native canvas.", 30.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    labels_.emplace_back(factory, L"Vector + text canvas", 17.0f);
    labels_.emplace_back(factory, L"Resize the window. The scene follows.", 14.0f);
    redraw();
}

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

void DemoWindow::on_resize() { invalidate(); }
void DemoWindow::on_paint() { redraw(); }
void DemoWindow::on_graphics_recreated() { brush_.reset(); }

std::optional<LRESULT> DemoWindow::on_message(UINT message, WPARAM, LPARAM lparam) {
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

    if (!brush_) { THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(0x24333F), brush_.put())); }
    const auto brush = brush_.get();
    brush->SetColor(D2D1::ColorF(0x24333F));
    for (float x = 32.0f; x < size.x; x += 32.0f) {
        for (float y = 120.0f; y < size.y - 88.0f; y += 32.0f) {
            dc->FillEllipse(D2D1::Ellipse({x, y}, 1.0f, 1.0f), brush);
        }
    }

    const auto text = [&](std::size_t index, float y, float height, UINT32 color) {
        auto& label = labels_[index];
        label.resize(std::max(1.0f, size.x - 64.0f), height);
        brush->SetColor(D2D1::ColorF(color));
        dc->DrawTextLayout({32, y}, label.layout().get(), brush);
    };

    text(0, 31, 24, 0x6FE6C8);
    text(1, 65, 47, 0xEAF2F4);

    const DirectX::SimpleMath::Vector2 center{size.x * 0.5f, size.y * 0.5f};
    brush->SetColor(D2D1::ColorF(0x314C57));
    dc->DrawEllipse(D2D1::Ellipse({center.x, center.y}, 142.0f, 142.0f), brush, 1.0f);
    dc->DrawEllipse(D2D1::Ellipse({center.x, center.y}, 157.0f, 157.0f), brush, 0.5f);

    brush->SetColor(D2D1::ColorF(0x31404A));
    dc->DrawLine({32.0f, size.y - 88.0f}, {size.x - 32.0f, size.y - 88.0f}, brush);
    text(2, size.y - 69, 25, 0xEAF2F4);
    text(3, size.y - 41, 25, 0x93A9B5);
    draw.finish();
    ++drawCount_;
}
