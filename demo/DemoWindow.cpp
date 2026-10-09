#include "DemoWindow.hpp"
#include "DemoButton.hpp"
#include <composia/AnimationHelpers.hpp>
#include "DemoAnimations.hpp"
#include <composia/ScopedSurfaceDraw.hpp>
#include <algorithm>
#include <array>
#include <string_view>

using namespace composia;

DemoWindow::DemoWindow(Application& app)
    : Window(app, L"Composia", 960, 640), app_(app), target_(app.compositor(), app.graphics(), hwnd()),
      motionButton_(*this, L"Change motion", demo::paint_button), resetButton_(*this, L"Reset", demo::paint_button) {
    scene_ = animations::container(app.compositor(), {960, 370});
    scene_.Offset({0, 166, 0});
    target_.root().Children().InsertAtTop(scene_);
    stage_ = animations::container(app.compositor(), {160.0f, 160.0f});
    scene_.Children().InsertAtTop(stage_);
    animations::center_in_parent(stage_, scene_);

    tile_ = animations::sprite(app.compositor(), {128.0f, 128.0f}, {255, 111, 230, 200});
    auto rounded = app.compositor().CreateRoundedRectangleGeometry();
    rounded.Size(tile_.Size());
    rounded.CornerRadius({28.0f, 28.0f});
    tile_.Clip(app.compositor().CreateGeometricClip(rounded));
    stage_.Children().InsertAtTop(tile_);
    demo::bob(tile_, {16.0f, 24.0f, 0.0f}, 22.0f);

    indicator_ = animations::sprite(app.compositor(), {8.0f, 8.0f}, {255, 111, 230, 200});
    target_.root().Children().InsertAtTop(indicator_);
    animations::implicit_offset(indicator_);
    demo::pulse(indicator_);
    const auto factory = app.graphics().text_factory().get();
    labels_.emplace_back(factory, L"COMPOSIA", 14.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    labels_.emplace_back(factory, L"A little motion. A native canvas.", 30.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    labels_.emplace_back(factory, L"Vector + text canvas", 17.0f);
    labels_.emplace_back(factory, L"Resize the window. The scene follows.", 14.0f);
    motionClick_ = motionButton_.on_click([this] {
        largeMotion_ = !largeMotion_;
        demo::bob(tile_, {16, 24, 0}, largeMotion_ ? 50.0f : 22.0f);
    });
    resetClick_ = resetButton_.on_click([this] {
        largeMotion_ = false;
        demo::bob(tile_, {16, 24, 0}, 22);
    });
    redraw();
}

void DemoWindow::redraw() {
    const auto bounds = client_bounds();
    if (bounds.width <= 0 || bounds.height <= 0 || IsIconic(hwnd())) {
        return;
    }
    scene_.Size({bounds.width, std::max(1.0f, bounds.height - 270)});
    const auto buttons = layout::stack({32, 116, std::max(0.0f, bounds.width - 64), 44},
        std::array{160.0f, 100.0f}, 12, layout::Axis::horizontal);
    motionButton_.set_bounds(buttons[0]);
    resetButton_.set_bounds(buttons[1]);
    indicator_.Offset({bounds.width - 42.0f, 42.0f, 0.0f});
    target_.render(*this, [&](ScopedSurfaceDraw& draw, numerics::float2 size) { draw_canvas(draw, size); });
}

void DemoWindow::on_resize() { invalidate(); }
void DemoWindow::on_paint() { redraw(); }

std::optional<LRESULT> DemoWindow::on_message(UINT message, WPARAM, LPARAM lparam) {
    if (message == WM_GETMINMAXINFO) {
        auto info = reinterpret_cast<MINMAXINFO*>(lparam);
        const auto windowDpi = hwnd() ? dpi() : GetDpiForSystem();
        info->ptMinTrackSize = {MulDiv(580, windowDpi, 96), MulDiv(540, windowDpi, 96)};
        return 0;
    }
    return std::nullopt;
}

void DemoWindow::draw_canvas(ScopedSurfaceDraw& draw, numerics::float2 size) {
    const auto dc = draw.context().get();
    dc->Clear(D2D1::ColorF(0x101923));

    const auto dots = draw.solid_brush(0x24333F);
    for (float x = 32.0f; x < size.x; x += 32.0f) {
        for (float y = 120.0f; y < size.y - 88.0f; y += 32.0f) {
            dc->FillEllipse(D2D1::Ellipse({x, y}, 1.0f, 1.0f), dots);
        }
    }

    const auto text = [&](std::size_t index, float y, float height, UINT32 color) {
        auto& label = labels_[index];
        label.resize(std::max(1.0f, size.x - 64.0f), height);
        dc->DrawTextLayout({32, y}, label.layout().get(), draw.solid_brush(color));
    };

    text(0, 31, 24, 0x6FE6C8);
    text(1, 65, 47, 0xEAF2F4);

    const D2D1_POINT_2F center{size.x * 0.5f, 166 + scene_.Size().y * 0.5f};
    const auto radius = std::clamp(scene_.Size().y * 0.5f - 20.0f, 60.0f, 142.0f);
    const auto ring = draw.solid_brush(0x314C57);
    dc->DrawEllipse(D2D1::Ellipse({center.x, center.y}, radius, radius), ring, 1.0f);
    dc->DrawEllipse(D2D1::Ellipse({center.x, center.y}, radius + 15, radius + 15), ring, 0.5f);

    dc->DrawLine({32.0f, size.y - 88.0f}, {size.x - 32.0f, size.y - 88.0f}, draw.solid_brush(0x31404A));
    text(2, size.y - 69, 25, 0xEAF2F4);
    text(3, size.y - 41, 25, 0x93A9B5);
    ++drawCount_;
}
