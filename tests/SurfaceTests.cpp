#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/TextLayout.hpp>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }

void verify_pixels(composia::Application& app, const composia::composition::CompositionDrawingSurface& surface,
    composia::TextLayout& text, UINT dpi) {
    composia::ScopedSurfaceDraw draw{surface, app.graphics(), dpi};
    const auto dc = draw.context().get();
    dc->Clear(D2D1::ColorF(D2D1::ColorF::Black));
    wil::com_ptr<ID2D1SolidColorBrush> brush;
    THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red), brush.put()));
    dc->FillRectangle({8, 8, 24, 24}, brush.get());
    brush->SetColor(D2D1::ColorF(D2D1::ColorF::White));
    text.resize(78, 27);
    dc->DrawTextLayout({32, 8}, text.layout().get(), brush.get());
    THROW_IF_FAILED(dc->Flush());

    wil::com_ptr<ID2D1Image> image;
    dc->GetTarget(image.put());
    auto bitmap = image.query<ID2D1Bitmap1>();
    const auto size = surface.Size();
    const D2D1_SIZE_U pixelSize{static_cast<UINT32>(size.Width), static_cast<UINT32>(size.Height)};
    auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        bitmap->GetPixelFormat());
    wil::com_ptr<ID2D1Bitmap1> readback;
    THROW_IF_FAILED(dc->CreateBitmap(pixelSize, nullptr, 0, properties, readback.put()));
    const auto offset = draw.update_offset();
    const D2D1_RECT_U source{static_cast<UINT32>(offset.x), static_cast<UINT32>(offset.y),
        static_cast<UINT32>(offset.x) + pixelSize.width, static_cast<UINT32>(offset.y) + pixelSize.height};
    THROW_IF_FAILED(readback->CopyFromBitmap(nullptr, bitmap.get(), &source));
    D2D1_MAPPED_RECT mapped{};
    THROW_IF_FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped));
    const auto unmap = wil::scope_exit([&] { LOG_IF_FAILED(readback->Unmap()); });
    const auto pixel = [&](unsigned x, unsigned y) { return mapped.bits + y * mapped.pitch + x * 4; };
    const auto scale = static_cast<float>(dpi) / 96.0f;
    const auto red = pixel(static_cast<unsigned>(16 * scale), static_cast<unsigned>(16 * scale));
    require(red[2] > 240 && red[1] < 8 && red[0] < 8 && red[3] == 255, "Rectangle pixels have wrong color or offset");
    const auto black = pixel(1, 1);
    require(black[0] == 0 && black[1] == 0 && black[2] == 0 && black[3] == 255, "Clear did not cover the surface");
    unsigned textPixels{};
    for (unsigned y = static_cast<unsigned>(8 * scale); y < static_cast<unsigned>(35 * scale); ++y) {
        for (unsigned x = static_cast<unsigned>(32 * scale); x < static_cast<unsigned>(110 * scale); ++x) {
            if (pixel(x, y)[0] > 100) { ++textPixels; }
        }
    }
    require(textPixels > 50, "Text glyph pixels were not rendered");
    draw.finish();
    std::cout << "dpi=" << dpi << " atlas_offset=" << offset.x << ',' << offset.y << " text_pixels=" << textPixels << '\n';
}
}

int main() {
    try {
        composia::Application app{true};
        {
            composia::Window window{app, L"Surface test", 320, 240};
            composia::CompositionWindowTarget target{app.compositor(), app.graphics(), window.hwnd()};
            composia::TextLayout text{app.graphics().text_factory().get(), L"Text", 18};
            for (UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
                target.resize({static_cast<LONG>(160 * dpi / 96), static_cast<LONG>(80 * dpi / 96)}, dpi);
                verify_pixels(app, target.surface(), text, dpi);
                app.graphics().recreate();
                verify_pixels(app, target.surface(), text, dpi);
            }
        }
        app.close();
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
