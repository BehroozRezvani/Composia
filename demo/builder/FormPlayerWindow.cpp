#include "FormPlayerWindow.hpp"
#include <composia/ScopedSurfaceDraw.hpp>
#include <windowsx.h>

namespace builder {

FormPlayerWindow::FormPlayerWindow(composia::Application& app, Document document)
    : Window(app, document.title().empty() ? std::wstring_view{L"Form"} : std::wstring_view{document.title()}, document.width(), document.height()),
      app_(app), target_(app.compositor(), app.graphics(), hwnd()), view_(*this, std::move(document)) {
    changed_ = view_.on_change([this](const Widget&) { invalidate(); });
    invalidate();
}

composia::numerics::float2 FormPlayerWindow::pointer(LPARAM lparam) const noexcept {
    const auto scale = static_cast<float>(dpi() ? dpi() : 96) / 96.0f;
    return {GET_X_LPARAM(lparam) / scale, GET_Y_LPARAM(lparam) / scale};
}

void FormPlayerWindow::on_paint() {
    const auto pixels = client_pixels();
    if (pixels.cx <= 0 || pixels.cy <= 0 || IsIconic(hwnd())) { return; }
    app_.render([&] {
        target_.resize(pixels, dpi());
        const auto size = target_.logical_size();
        view_.arrange({0, 0, size.x, size.y});
        composia::ScopedSurfaceDraw draw{target_.surface(), app_.graphics(), dpi()};
        const auto dc = draw.context().get();
        dc->Clear(D2D1::ColorF(palette::form));
        if (!brush_) { THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(palette::ink), brush_.put())); }
        FormPainter painter{dc, draw.text_factory().get(), brush_.get()};
        view_.draw(painter);
        draw.finish();
        ++drawCount_;
    });
}

std::optional<LRESULT> FormPlayerWindow::on_message(UINT message, WPARAM, LPARAM lparam) {
    switch (message) {
    case WM_GETMINMAXINFO: {
        const auto info = reinterpret_cast<MINMAXINFO*>(lparam);
        const auto windowDpi = hwnd() ? dpi() : GetDpiForSystem();
        info->ptMinTrackSize = {MulDiv(240, windowDpi, 96), MulDiv(160, windowDpi, 96)};
        return 0;
    }
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hwnd(), &point);
            SetCursor(LoadCursorW(nullptr, view_.interactive_at(pointer(MAKELPARAM(point.x, point.y))) ? IDC_HAND : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd());
        if (view_.pointer_down(pointer(lparam)) == FormView::Pointer::dragging) { SetCapture(hwnd()); }
        return 0;
    case WM_MOUSEMOVE:
        if (view_.dragging()) { view_.pointer_move(pointer(lparam)); }
        return 0;
    case WM_LBUTTONUP:
        if (view_.dragging()) { view_.pointer_up(); ReleaseCapture(); }
        return 0;
    case WM_CAPTURECHANGED:
    case WM_CANCELMODE:
        view_.pointer_up();
        return 0;
    }
    return std::nullopt;
}

}
