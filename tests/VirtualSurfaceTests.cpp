#include <composia/Application.hpp>
#include <composia/VirtualSurface.hpp>
#include "support/TestSupport.hpp"
#include <iostream>
#include <stdexcept>

using namespace composia;
using testing::require;
using testing::rejects;

namespace {
void expect_pixel(Application& app, const VirtualSurface& surface, LONG x, LONG y, bool red) {
    const auto pixel = testing::surface_pixel(app, surface.surface(), x, y);
    require(pixel.a == 255 && testing::matches(pixel, red ? 0xFF0000 : 0x00FF00, 8), "Wrong pixels in retained virtual surface region");
}

int sparse_cache(const testing::Options& options) {
    Application app{options.warp};
    VirtualSurface surface{app.graphics(), {1 << 20, 1 << 20}, 256, 64};
    require(surface.size().cx == 1 << 20 && surface.size().cy == 1 << 20, "The logical size was not kept");
    require(surface.surface() != nullptr && surface.cached_tiles() == 0, "A new surface was not empty");
    std::size_t paints{};
    bool red = true;
    const VirtualSurface::Painter paint = [&](ScopedSurfaceDraw& draw, const RECT&) {
        ++paints;
        draw.context()->Clear(D2D1::ColorF(red ? 0xFF0000 : 0x00FF00));
    };
    surface.update({0, 0, 400, 400}, paint);
    require(paints == 9, "Initial viewport did not draw 3x3 tiles including overscan");
    const auto retained = surface.retained_region();
    require(retained.left == 0 && retained.top == 0 && retained.right == 768 && retained.bottom == 768,
        "The retained region did not cover the viewport tiles and overscan");
    expect_pixel(app, surface, 20, 20, true);
    surface.update({10, 10, 390, 390}, paint);
    require(paints == 9, "Cached tiles were unnecessarily redrawn");
    expect_pixel(app, surface, 20, 20, true);  // Trim must preserve the supplied region.
    red = false;
    surface.invalidate({1, 1, 40, 40});
    surface.invalidate({50, 50, 50, 90});  // Empty: nothing to redraw.
    surface.update({10, 10, 390, 390}, paint);
    require(paints == 10, "Dirty region did not redraw exactly one tile");
    expect_pixel(app, surface, 20, 20, false);
    expect_pixel(app, surface, 300, 20, true);
    for (LONG i = 1; i <= 80; ++i) {
        const LONG offset = i * 12000;
        surface.update({offset, offset, offset + 400, offset + 400}, paint);
        // A 400-pixel view can cross three tiles, plus one overscan tile on either side.
        require(surface.cached_tiles() <= 25 && surface.retained_pixel_bytes() <= 25 * 256 * 256 * 4,
            "Virtual surface cache grew with scrolling history");
    }
    expect_pixel(app, surface, 960010, 960010, false);
    const auto beforeReturn = paints;
    surface.update({0, 0, 400, 400}, paint);
    require(paints == beforeReturn + 9, "Trimmed tiles were reused without repainting");
    expect_pixel(app, surface, 20, 20, false);
    const auto beforeRecovery = paints;
    app.graphics().recreate();
    surface.update({0, 0, 400, 400}, paint);
    require(paints == beforeRecovery + 9, "Device replacement reused lost tile contents");
    expect_pixel(app, surface, 20, 20, false);
    require(rejects(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY), [&] { surface.update({0, 0, 20000, 20000}, paint); }) &&
        surface.cached_tiles() == 9, "Oversized viewport did not preserve bounded cache");
    surface.clear();
    require(surface.cached_tiles() == 0 && surface.retained_pixel_bytes() == 0, "Clear retained tiles");
    require(testing::throws<std::runtime_error>([&] {
        surface.update({0, 0, 200, 200}, [](auto&, const auto&) { throw std::runtime_error("paint failure"); });
    }) && surface.cached_tiles() == 0, "Failed paint marked an incomplete tile valid");
    surface.update({0, 0, 200, 200}, paint);
    expect_pixel(app, surface, 20, 20, false);
    surface.resize({333, 271});
    surface.resize({333, 271});  // The same size keeps the cache.
    surface.update({0, 0, 333, 271}, paint);
    require(surface.cached_tiles() == 4 && surface.retained_pixel_bytes() == 333 * 271 * 4,
        "Resize did not clip edge tiles");
    expect_pixel(app, surface, 332, 270, false);
    surface.resize({VirtualSurface::maximum_dimension, VirtualSurface::maximum_dimension});
    const LONG edge = VirtualSurface::maximum_dimension;
    surface.update({edge - 200, edge - 200, edge, edge}, paint);
    expect_pixel(app, surface, edge - 1, edge - 1, false);
    surface.update({-100, -100, 0, 0}, paint);
    require(surface.cached_tiles() == 0, "Empty viewport did not release cache");
    std::cout << "sparse_cache=true invalidation=true recovery=true maximum_extent=true\n";
    return 0;
}

int arguments(const testing::Options& options) {
    Application app{options.warp};
    auto& graphics = app.graphics();
    const auto invalid = [&](auto&& action, const char* message) { require(rejects(E_INVALIDARG, action), message); };
    invalid([&] { VirtualSurface surface{graphics, {0, 10}}; }, "An empty surface was accepted");
    invalid([&] { VirtualSurface surface{graphics, {VirtualSurface::maximum_dimension + 1, 10}}; }, "An oversized surface was accepted");
    invalid([&] { VirtualSurface surface{graphics, {100, 100}, 63}; }, "A tile size below 64 was accepted");
    invalid([&] { VirtualSurface surface{graphics, {100, 100}, 2049}; }, "A tile size above 2048 was accepted");
    invalid([&] { VirtualSurface surface{graphics, {100, 100}, 256, 0}; }, "A cache without tiles was accepted");
    VirtualSurface surface{graphics, {1000, 1000}};
    invalid([&] { surface.resize({-1, 10}); }, "A negative size was accepted");
    invalid([&] { surface.invalidate({10, 10, 5, 20}); }, "An inverted dirty rectangle was accepted");
    invalid([&] { surface.update({0, 0, 100, 100}, {}); }, "A missing painter was accepted");
    invalid([&] { surface.update({10, 10, 0, 100}, [](auto&, const auto&) {}); }, "An inverted viewport was accepted");
    std::cout << "invalid_arguments_rejected=true\n";
    return 0;
}

int partial_updates(const testing::Options& options) {
    Application app{options.warp};
    VirtualSurface surface{app.graphics(), {1 << 20, 1 << 20}};
    const RECT update{960000, 480000, 960128, 480128};
    for (UINT dpi : {96u, 120u, 144u, 168u, 192u}) {
        ScopedSurfaceDraw draw{surface.surface(), app.graphics(), dpi, update};
        const auto dc = draw.context().get();
        dc->Clear(D2D1::ColorF(0x00FF00));
        const float factor = 96.0f / static_cast<float>(dpi);
        dc->FillRectangle({(update.left + 16) * factor, (update.top + 16) * factor,
            (update.left + 80) * factor, (update.top + 80) * factor}, draw.solid_brush(0xFF0000));
        draw.finish();
        expect_pixel(app, surface, update.left + 32, update.top + 32, true);
        expect_pixel(app, surface, update.left + 4, update.top + 4, false);
        std::cout << "partial_update_dpi=" << dpi << '\n';
    }
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {
        {"sparse-cache", sparse_cache},
        {"arguments", arguments},
        {"partial-updates", partial_updates},
    });
}
