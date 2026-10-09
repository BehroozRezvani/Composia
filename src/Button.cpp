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

// The colors of a button without a painter, in one state.
struct Palette {
    std::uint32_t face, text, border, focus;
};

Palette palette(const Button::State& state) {
    const auto& look = state.appearance;
    if (look.highContrast) {
        // Contrast themes allow only their own colors: hot and pressed buttons take the highlight.
        const auto& colors = look.colors;
        const bool hot = state.enabled && (state.hovered || state.pressed);
        return {hot ? colors.highlight : colors.buttonFace,
            !state.enabled ? colors.grayText : hot ? colors.highlightText : colors.buttonText,
            state.enabled ? colors.buttonText : colors.grayText,
            hot ? colors.highlightText : colors.buttonText};
    }
    if (look.dark) {
        return {!state.enabled ? 0x2A2A2Au : state.pressed ? 0x262626u : state.hovered ? 0x383838u : 0x2D2D2Du,
            state.enabled ? 0xFFFFFFu : 0x787878u, 0x434343u, 0xFFFFFFu};
    }
    return {!state.enabled ? 0xF5F5F5u : state.pressed ? 0xE6E6E6u : state.hovered ? 0xF3F3F3u : 0xFDFDFDu,
        state.enabled ? 0x1B1B1Bu : 0x9E9E9Eu, 0xD1D1D1u, 0x1B1B1Bu};
}
}

Button::Button(Window& parent, std::wstring_view label, Painter painter)
    : Window(parent.application(), label, 160, 44, require_parent(parent)),
      label_(label),
      painter_(std::move(painter)),
      target_(application().compositor(), application().graphics(), hwnd()),
      text_(make_label(label_)),
      accessible_(*this, {.name = label_, .controlType = UIA_ButtonControlTypeId, .invoke = [this] { invoke(); }}) {
    invalidate();
}

Button::~Button() = default;

// The label, centered in the box the paint sizes it to.
TextLayout Button::make_label(std::wstring_view label) const {
    TextLayout text{application().graphics().text_factory().get(), label, 14};
    THROW_IF_FAILED(text.layout()->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER));
    THROW_IF_FAILED(text.layout()->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
    return text;
}

void Button::set_painter(Painter painter) {
    painter_ = std::move(painter);
    invalidate();
}

void Button::set_label(std::wstring_view label) {
    (void)require_hwnd();
    std::wstring owned{label};
    auto text = make_label(owned);
    THROW_IF_WIN32_BOOL_FALSE(SetWindowTextW(hwnd(), owned.c_str()));
    text_ = std::move(text);
    label_ = std::move(owned);
    accessible_.set_name(label_);
    invalidate();
}

bool Button::pressed() const noexcept { return keyPressed_ || (mousePressed_ && hovered()); }

void Button::invoke() {
    (void)require_hwnd();
    if (!enabled()) { return; }
    accessible_.raise_invoked();
    clicked_.emit();
}

void Button::paint_default(ScopedSurfaceDraw& draw, numerics::float2 size, const State& state) {
    const auto colors = palette(state);
    const auto dc = draw.context().get();
    dc->Clear(D2D1::ColorF(colors.face));
    dc->DrawRectangle({0.5f, 0.5f, std::max(0.5f, size.x - 0.5f), std::max(0.5f, size.y - 0.5f)}, draw.solid_brush(colors.border));
    if (state.focused) {
        dc->DrawRectangle({3, 3, std::max(3.0f, size.x - 3), std::max(3.0f, size.y - 3)}, draw.solid_brush(colors.focus), 2);
    }
    dc->DrawTextLayout({0, 0}, state.label.layout().get(), draw.solid_brush(colors.text));
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
        text_.resize(std::max(1.0f, size.x), std::max(1.0f, size.y));
        const State state{text_, application().appearance(), hovered(), pressed(), focused(), enabled()};
        if (painter_) { painter_(draw, size, state); }
        else { paint_default(draw, size, state); }
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
