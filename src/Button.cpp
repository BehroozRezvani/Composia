#include <composia/Button.hpp>
#include <composia/Application.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <algorithm>
#include <string>

namespace composia {
namespace {
HWND require_parent(const Window& parent) {
    const auto hwnd = parent.hwnd();
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), !hwnd);
    return hwnd;
}
}

Button::Button(Window& parent, std::wstring_view label)
    : Window(parent.application(), label, 160, 44, require_parent(parent)),
      target_(application().compositor(), application().graphics(), hwnd()),
      text_(application().graphics().text_factory().get(), label, 15, DWRITE_FONT_WEIGHT_SEMI_BOLD),
      accessible_(*this, {.name = std::wstring{label}, .controlType = UIA_ButtonControlTypeId, .invoke = [this] { invoke(); }}) {
    THROW_IF_FAILED(text_.layout()->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
    THROW_IF_FAILED(text_.layout()->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
    invalidate();
}

Button::~Button() = default;

void Button::invoke() {
    (void)require_hwnd();
    if (!enabled()) { return; }
    accessible_.raise_invoked();
    clicked_.emit();
}

void Button::on_resize() { invalidate(); }
void Button::on_hover(bool) { invalidate(); }

void Button::on_focus(bool focused) {
    if (focused) { invalidate(); } else { cancel_press(); }
}

void Button::on_capture_lost() {
    mousePressed_ = false;
    invalidate();
}

void Button::on_enabled(bool value) {
    if (!value) { cancel_press(); }
    invalidate();
}

void Button::on_paint() {
    target_.render(*this, [&](ScopedSurfaceDraw& draw, numerics::float2 size) {
        const auto dc = draw.context().get();
        dc->Clear(D2D1::ColorF(0x101923));
        // Hover follows the captured pointer, so dragging off a pressed button drops the pressed look.
        const bool pressed = keyPressed_ || (mousePressed_ && hovered());
        const UINT32 fill = !enabled() ? 0x35434B : pressed ? 0x3DAB93 : hovered() ? 0xA2F5DF : 0x6FE6C8;
        const auto bounds = D2D1::RoundedRect({2, 2, std::max(2.0f, size.x - 2), std::max(2.0f, size.y - 2)}, 8, 8);
        dc->FillRoundedRectangle(bounds, draw.solid_brush(fill));
        if (focused()) {
            dc->DrawRoundedRectangle(bounds, draw.solid_brush(D2D1::ColorF(D2D1::ColorF::White)), 2);
        }
        text_.resize(std::max(1.0f, size.x), std::max(1.0f, size.y));
        dc->DrawTextLayout({0, 0}, text_.layout().get(), draw.solid_brush(enabled() ? 0x101923 : 0x93A9B5));
    });
}

bool Button::hit_test(LPARAM position) const {
    const auto bounds = client_bounds();
    const auto point = pointer_position(position);
    return bounds.contains(point.x, point.y);
}

void Button::cancel_press() {
    mousePressed_ = keyPressed_ = false;
    release_pointer();
    invalidate();
}

std::optional<LRESULT> Button::on_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_GETDLGCODE:
        return DLGC_BUTTON | ((wparam == VK_SPACE || wparam == VK_RETURN) ? DLGC_WANTMESSAGE : 0);
    case WM_LBUTTONDOWN:
        if (enabled() && hit_test(lparam)) {
            focus();
            capture_pointer();
            mousePressed_ = true;
            invalidate();
        }
        return 0;
    case WM_LBUTTONUP: {
        const bool activate = mousePressed_ && hit_test(lparam);
        cancel_press();
        if (activate) { invoke(); }
        return 0;
    }
    case WM_CANCELMODE:
        keyPressed_ = false;
        invalidate();
        return 0;
    case WM_KEYDOWN:
        if (enabled() && focused() && !(lparam & (1LL << 30))) {
            if (wparam == VK_SPACE) { keyPressed_ = true; invalidate(); return 0; }
            if (wparam == VK_RETURN) { invoke(); return 0; }
        }
        break;
    case WM_KEYUP:
        if (wparam == VK_SPACE) {
            const bool activate = keyPressed_;
            keyPressed_ = false;
            invalidate();
            if (activate) { invoke(); }
            return 0;
        }
        break;
    case WM_CHAR:
        if (wparam == VK_SPACE || wparam == VK_RETURN) { return 0; }
        break;
    }
    return std::nullopt;
}

}
