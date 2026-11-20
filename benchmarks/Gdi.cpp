#include "Scene.hpp"
#include <wil/resource.h>
#include <wil/result.h>

struct Gdi {
    wil::unique_hdc dc{CreateCompatibleDC(nullptr)};
    wil::unique_hbitmap bitmap;
    HGDIOBJ previous{};
    unsigned* pixels{};
    std::array<wil::unique_hbrush, 3> brushes;
    Gdi() {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = bench::width;
        info.bmiHeader.biHeight = -bench::height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* bits{};
        bitmap.reset(CreateDIBSection(dc.get(), &info, DIB_RGB_COLORS, &bits, nullptr, 0));
        THROW_LAST_ERROR_IF(!bitmap);
        previous = SelectObject(dc.get(), bitmap.get());
        THROW_LAST_ERROR_IF(!previous);
        pixels = static_cast<unsigned*>(bits);
        for (size_t i = 0; i < brushes.size(); ++i) {
            const auto c = bench::palette[i];
            brushes[i].reset(CreateSolidBrush(RGB(c >> 16, c >> 8, c)));
            THROW_LAST_ERROR_IF(!brushes[i]);
        }
    }
    ~Gdi() { SelectObject(dc.get(), previous); }
    unsigned paint(int frame) {
        bench::scene(frame, [&](RECT r, int index) {
            THROW_IF_WIN32_BOOL_FALSE(FillRect(dc.get(), &r, brushes[index].get()));
        });
        THROW_IF_WIN32_BOOL_FALSE(GdiFlush());
        return pixels[8 * bench::width + 8] & 0xffffff;
    }
};
int wmain(int argc, wchar_t** argv) { return bench::run<Gdi>(argc, argv); }
