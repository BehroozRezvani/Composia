#include "TextBoxHost.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace composia {
namespace {
std::wstring terminated(std::wstring_view text) {
    if (text.size() >= LONG_MAX || text.find(L'\0') != text.npos) {
        throw std::invalid_argument("Rich Edit text must fit a LONG and contain no embedded NUL");
    }
    return std::wstring(text);
}
D2D1_COLOR_F color(COLORREF c) {
    return D2D1::ColorF(GetRValue(c) / 255.f, GetGValue(c) / 255.f, GetBValue(c) / 255.f);
}
}

namespace {
void check_style(const TextBox::Options& o) {
    if (!std::isfinite(o.fontSize) || o.fontSize <= 0 || o.fontSize > 1638 ||
        !std::isfinite(o.padding) || o.padding < 0 || o.padding > 32767 ||
        o.fontFamily.empty() || o.fontFamily.size() >= LF_FACESIZE ||
        o.fontFamily.find(L'\0') != std::wstring::npos) {
        throw std::invalid_argument("Invalid text box font or padding");
    }
}
layout::Size size_for(const TextBox::Options& o) {
    return {180, (o.multiline ? 4.f : 1.f) * std::ceil(o.fontSize * 1.5f) + 2 * o.padding};
}
}

TextBox::TextBox(Options options, std::wstring_view initial, Painter painter)
    : options_(std::move(options)), painter_(std::move(painter)) {
    check_style(options_);
    host_.attach(new Host(*this));
    auto rollback = wil::scope_exit([&] { host_->shutdown(); });
    host_->initialize();
    set_text(initial);
    set_focusable(true);
    set_cursor(Cursor::text);
    set_preferred_size(size_for(options_));
    rollback.release();
}

TextBox::~TextBox() {
    host_->shutdown();
}
std::wstring TextBox::text() const {
    wil::unique_bstr value;
    THROW_IF_FAILED(host_->engine->TxGetText(value.put()));
    return value ? std::wstring(value.get(), SysStringLen(value.get())) : std::wstring{};
}
void TextBox::set_text(std::wstring_view text) {
    const auto value = terminated(text);
    THROW_IF_FAILED(host_->engine->TxSetText(value.c_str()));
    host_->send(EM_EMPTYUNDOBUFFER);
    mark_saved();
    invalidate();
}
bool TextBox::modified() const {
    return host_->send(EM_GETMODIFY) != 0;
}
void TextBox::mark_saved() {
    host_->send(EM_SETMODIFY, FALSE);
}
TextBox::Selection TextBox::selection() const {
    CHARRANGE r{};
    host_->send(EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&r));
    return {r.cpMin, r.cpMax};
}
void TextBox::select(long first, long last) {
    CHARRANGE r{first, last};
    host_->send(EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&r));
    host_->send(EM_SCROLLCARET);
    invalidate();
}
void TextBox::replace_selection(std::wstring_view text) {
    if (options_.readOnly) { return; }
    auto value = terminated(text);
    host_->send(EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(value.c_str()));
}
bool TextBox::undo() {
    return !options_.readOnly && host_->send(EM_UNDO) != 0;
}
bool TextBox::redo() {
    return !options_.readOnly && host_->send(EM_REDO) != 0;
}
void TextBox::copy() {
    host_->send(WM_COPY);
}
void TextBox::cut() {
    if (!options_.readOnly) { host_->send(WM_CUT); }
}
void TextBox::paste() {
    if (!options_.readOnly) { host_->send(EM_PASTESPECIAL, CF_UNICODETEXT); }
}
void TextBox::set_read_only(bool value) {
    if (options_.readOnly == value) { return; }
    options_.readOnly = value;
    THROW_IF_FAILED(host_->engine->OnTxPropertyBitsChange(TXTBIT_READONLY, value ? TXTBIT_READONLY : 0));
    invalidate();
}
bool TextBox::can_undo() const {
    return !options_.readOnly && host_->send(EM_CANUNDO) != 0;
}
bool TextBox::can_redo() const {
    return !options_.readOnly && host_->send(EM_CANREDO) != 0;
}
void TextBox::set_style(const Options& style) {
    check_style(style);
    options_.fontFamily = style.fontFamily;
    options_.fontSize = style.fontSize;
    options_.padding = style.padding;
    options_.foreground = style.foreground;
    options_.background = style.background;
    options_.selectionColor = style.selectionColor;
    options_.selectedTextColor = style.selectedTextColor;
    host_->restyle();
    set_preferred_size(size_for(options_));
    invalidate();
}
LRESULT TextBox::send(UINT message, WPARAM wparam, LPARAM lparam) {
    return host_->send(message, wparam, lparam);
}
ITextServices2* TextBox::text_services() const noexcept { return host_->engine.get(); }
void TextBox::set_painter(Painter painter) {
    painter_ = std::move(painter);
    invalidate();
}

void TextBox::paint(ScopedSurfaceDraw& draw) {
    host_->sync();
    if (painter_) {
        painter_(draw, *this);
        return;
    }
    const auto b = bounds();
    auto dc = draw.context().get();
    dc->FillRectangle({0, 0, b.width, b.height}, draw.solid_brush(color(options_.background)));
    draw_text(draw);
    draw_caret(draw);
    if (b.width >= 1 && b.height >= 1) {
        dc->DrawRectangle({0.5f, 0.5f, b.width - 0.5f, b.height - 0.5f},
                          draw.solid_brush(color(focused() ? options_.selectionColor : GetSysColor(COLOR_GRAYTEXT))));
    }
}
void TextBox::draw_text(ScopedSurfaceDraw& draw) {
    host_->sync();
    const auto r = host_->client();
    const RECTL box{r.left, r.top, r.right, r.bottom};
    const auto origin = viewport_bounds();
    const auto& damage = draw.update_bounds();
    // Rich Edit accepts integer local coordinates. Round out so antialiased edge pixels are included.
    RECT update{LONG(std::floor(std::clamp(damage.left - origin.x, 0.f, float(r.right)))),
                LONG(std::floor(std::clamp(damage.top - origin.y, 0.f, float(r.bottom)))),
                LONG(std::ceil(std::clamp(damage.right - origin.x, 0.f, float(r.right)))),
                LONG(std::ceil(std::clamp(damage.bottom - origin.y, 0.f, float(r.bottom))))};
    if (IsRectEmpty(&update)) { return; }
    D2D1_MATRIX_3X2_F transform{};
    auto* dc = draw.context().get();
    dc->GetTransform(&transform);
    const auto restore = wil::scope_exit([&] { dc->SetTransform(transform); });
    THROW_IF_FAILED(host_->engine->TxDrawD2D(dc, &box, &update, host_->inPlace ? TXTVIEW_ACTIVE : TXTVIEW_INACTIVE));
}
layout::Rect TextBox::caret_bounds() const noexcept {
    const auto r = host_->caret;
    return {float(r.left), float(r.top), float(r.right - r.left), float(r.bottom - r.top)};
}
void TextBox::draw_caret(ScopedSurfaceDraw& draw) {
    if (!caret_visible()) { return; }
    const auto r = caret_bounds();
    draw.context()->FillRectangle({r.x, r.y, r.x + r.width, r.y + r.height},
                                  draw.solid_brush(color(options_.foreground)));
}
bool TextBox::caret_visible() const noexcept {
    return host_->caretOn && focused();
}
bool TextBox::pointer_pressed() const noexcept { return host_->captured; }
void TextBox::on_focus_changed(bool value) {
    host_->focus(value && host_window() && host_window()->focused());
}
void TextBox::on_detaching() noexcept {
    host_->deactivate();
}
bool TextBox::on_key(const KeyEvent& e) {
    if (e.key == VK_TAB || e.alt || (e.key == VK_RETURN && !options_.multiline)) { return false; }
    return host_->input(e.down ? WM_KEYDOWN : WM_KEYUP, e.key, e.keyData).has_value();
}
bool TextBox::on_pointer(const PointerEvent& e) {
    host_->sync();
    const auto pt = MAKELPARAM(std::lround(e.position.x), std::lround(e.position.y));
    WPARAM keys = e.modifiers;
    if (e.kind == PointerEvent::Kind::wheel) {
        POINT p{std::lround(e.position.x), std::lround(e.position.y)};
        if (!host_->TxClientToScreen(&p)) { return false; }
        POINT before{}, after{};
        host_->send(EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&before));
        host_->input(e.wheelAxis == layout::Axis::vertical ? WM_MOUSEWHEEL : WM_MOUSEHWHEEL,
                     MAKEWPARAM(keys, std::lround(e.wheelDelta * WHEEL_DELTA *
                                                  (e.wheelAxis == layout::Axis::vertical ? 1 : -1))),
                     MAKELPARAM(p.x, p.y));
        host_->send(EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&after));
        return before.x != after.x || before.y != after.y;
    }
    if (e.kind == PointerEvent::Kind::move) {
        return host_->input(WM_MOUSEMOVE, keys, pt).has_value();
    }
    if (e.button != PointerEvent::Button::left) { return false; }
    if (e.kind == PointerEvent::Kind::down) { keys |= MK_LBUTTON; }
    host_->input(e.kind == PointerEvent::Kind::up ? WM_LBUTTONUP
                 : e.doubleClick                  ? WM_LBUTTONDBLCLK
                                                  : WM_LBUTTONDOWN,
                 keys, pt);
    return true;
}

std::optional<LRESULT> TextBox::window_message(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CHAR:
        if (w == L'\t' || w == VK_ESCAPE || (w == L'\r' && !options_.multiline)) { return 0; }
        return host_->input(msg, w, l);
    case WM_UNICHAR:
        if (w == UNICODE_NOCHAR) { return TRUE; }
        if (w > 0x10ffff || (w >= 0xd800 && w <= 0xdfff)) { return 0; }
        if (w < 0x10000) { return window_message(WM_CHAR, w, l); }
        host_->input(WM_CHAR, 0xd800 + ((w - 0x10000) >> 10), l);
        return host_->input(WM_CHAR, 0xdc00 + ((w - 0x10000) & 1023), l);
    case WM_COPY:
        copy();
        return 0;
    case WM_CUT:
        cut();
        return 0;
    case WM_PASTE:
        paste();
        return 0;
    case WM_CLEAR:
    case WM_UNDO:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_SETCONTEXT:
    case WM_IME_REQUEST:
    case WM_IME_NOTIFY:
    case WM_IME_CHAR:
    case WM_IME_KEYDOWN:
    case WM_IME_KEYUP:
    case WM_INPUTLANGCHANGE:
        return host_->input(msg, w, l);
    case WM_CANCELMODE:
        host_->TxSetCapture(FALSE);
        return host_->input(msg, w, l);
    case WM_CAPTURECHANGED:
        if (host_->captured) {
            host_->captured = false;
            return host_->input(msg, w, l);
        }
        break;
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
        if (host_->captured) {
            const auto p = host_window()->pointer_position(l);
            const auto b = viewport_bounds();
            return host_->input(msg, w, MAKELPARAM(std::lround(p.x - b.x), std::lround(p.y - b.y)));
        }
        break;
    }
    return std::nullopt;
}

}
