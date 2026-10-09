#include "TextField.hpp"
#include <composia/ScopedSurfaceDraw.hpp>
#include <windowsx.h>
#include <algorithm>
#include <cwctype>

using namespace composia;
using namespace std::chrono_literals;

namespace {
constexpr float padX = 12, padY = 8, fontSize = 14;
constexpr UINT32 surround = 0x131D28, fill = 0x0E161F, border = 0x2A3946, accent = 0x6FE6C8, ink = 0xEAF2F4, hint = 0x5F7380;

HWND require_parent(const Window& parent) {
    THROW_HR_IF(HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE), !parent.hwnd());
    return parent.hwnd();
}

bool control_down() noexcept { return GetKeyState(VK_CONTROL) < 0; }

std::wstring normalize(std::wstring_view value, bool multiline) {
    std::wstring result;
    result.reserve(value.size());
    for (const wchar_t c : value) {
        if (c == L'\r') { continue; }
        if (c == L'\n') { result.push_back(multiline ? L'\n' : L' '); }
        else if (c == L'\t' || c >= 0x20) { result.push_back(c); }
    }
    return result;
}
}

TextField::TextField(Window& parent, std::wstring_view placeholder, bool multiline)
    : Window(parent.application(), placeholder, 240, 36, require_parent(parent)),
      target_(application().compositor(), application().graphics(), hwnd()), placeholder_(placeholder), multiline_(multiline) {
    const auto compositor = application().compositor();
    caret_visual_ = compositor.CreateSpriteVisual();
    caret_visual_.Size({1.5f, fontSize + 4});
    caret_visual_.Brush(compositor.CreateColorBrush({255, 111, 230, 200}));
    caret_visual_.IsVisible(false);
    target_.root().Children().InsertAtTop(caret_visual_);
    invalidate();
}

TextField::~TextField() = default;

void TextField::set_text(std::wstring value) {
    text_ = normalize(value, multiline_);
    caret_ = text_.size();
    scroll_ = 0;
    changed();
}

void TextField::insert(std::wstring_view value) {
    const auto clean = normalize(value, multiline_);
    if (clean.empty()) { return; }
    text_.insert(caret_, clean);
    caret_ += clean.size();
    changed();
}

void TextField::focus() { SetFocus(require_hwnd()); }

void TextField::changed() {
    (void)require_hwnd();
    // Restarting the blink keeps the caret solid while typing.
    auto animation = application().compositor().CreateScalarKeyFrameAnimation();
    animation.InsertKeyFrame(0.0f, 1.0f);
    animation.InsertKeyFrame(0.5f, 1.0f);
    animation.InsertKeyFrame(0.52f, 0.0f);
    animation.InsertKeyFrame(1.0f, 0.0f);
    animation.Duration(1000ms);
    animation.IterationBehavior(composition::AnimationIterationBehavior::Forever);
    caret_visual_.StartAnimation(L"Opacity", animation);
    invalidate();
    changed_.emit();
}

std::size_t TextField::step(std::size_t position, int direction) const noexcept {
    const auto pair = [&](std::size_t low) { return low > 0 && low < text_.size() && IS_LOW_SURROGATE(text_[low]) && IS_HIGH_SURROGATE(text_[low - 1]); };
    if (direction < 0) {
        if (position == 0) { return 0; }
        --position;
        if (pair(position)) { --position; }
    } else {
        if (position >= text_.size()) { return text_.size(); }
        ++position;
        if (pair(position)) { ++position; }
    }
    return position;
}

void TextField::move_caret(std::size_t position) {
    caret_ = std::min(position, text_.size());
    changed_.emit();
    invalidate();
}

TextLayout TextField::build_layout(float width, float height) const {
    TextLayout layout{application().graphics().text_factory().get(), text_, fontSize};
    layout.resize(std::max(1.0f, width), std::max(1.0f, height));
    THROW_IF_FAILED(layout.layout()->SetWordWrapping(multiline_ ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP));
    THROW_IF_FAILED(layout.layout()->SetParagraphAlignment(multiline_ ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
    return layout;
}

void TextField::place_caret(float x, float y) {
    const auto pixels = client_pixels();
    const auto scale = static_cast<float>(dpi()) / 96.0f;
    const auto layout = build_layout(pixels.cx / scale - 2 * padX, pixels.cy / scale - 2 * padY);
    BOOL trailing{}, inside{};
    DWRITE_HIT_TEST_METRICS metrics{};
    THROW_IF_FAILED(layout.layout()->HitTestPoint(x, y, &trailing, &inside, &metrics));
    move_caret(metrics.textPosition + (trailing ? metrics.length : 0));
}

void TextField::move_line(int direction) {
    const auto pixels = client_pixels();
    const auto scale = static_cast<float>(dpi()) / 96.0f;
    const auto layout = build_layout(pixels.cx / scale - 2 * padX, pixels.cy / scale - 2 * padY);
    float x{}, y{};
    DWRITE_HIT_TEST_METRICS metrics{};
    THROW_IF_FAILED(layout.layout()->HitTestTextPosition(static_cast<UINT32>(caret_), FALSE, &x, &y, &metrics));
    DWRITE_TEXT_METRICS extent{};
    THROW_IF_FAILED(layout.layout()->GetMetrics(&extent));
    const float target = y + (direction > 0 ? metrics.height * 1.5f : -metrics.height * 0.5f);
    if (target < 0) { move_caret(0); return; }
    if (target > extent.height) { move_caret(text_.size()); return; }
    place_caret(x, target);
}

void TextField::erase(bool backward, bool word) {
    std::size_t start = caret_, end = caret_;
    const auto space = [&](std::size_t index) { return std::iswspace(text_[index]) != 0; };
    if (backward) {
        if (start == 0) { return; }
        start = step(start, -1);
        if (word) {
            while (start > 0 && space(start)) { --start; }
            while (start > 0 && !space(start - 1)) { --start; }
        }
    } else {
        if (end >= text_.size()) { return; }
        end = step(end, 1);
        if (word) {
            while (end < text_.size() && !space(end)) { ++end; }
            while (end < text_.size() && space(end)) { ++end; }
        }
    }
    text_.erase(start, end - start);
    caret_ = start;
    changed();
}

void TextField::paste() {
    if (!OpenClipboard(hwnd())) { return; }
    const auto close = wil::scope_exit([] { CloseClipboard(); });
    const auto data = GetClipboardData(CF_UNICODETEXT);
    if (!data) { return; }
    const auto chars = static_cast<const wchar_t*>(GlobalLock(data));
    if (!chars) { return; }
    const auto unlock = wil::scope_exit([&] { GlobalUnlock(data); });
    insert(chars);
}

void TextField::on_paint() {
    const auto pixels = client_pixels();
    if (pixels.cx <= 0 || pixels.cy <= 0) { return; }
    application().render([&] {
        target_.resize(pixels, dpi());
        const auto size = target_.logical_size();
        const float innerWidth = std::max(1.0f, size.x - 2 * padX), innerHeight = std::max(1.0f, size.y - 2 * padY);
        const auto layout = build_layout(innerWidth, innerHeight);
        float caretX{}, caretY{};
        DWRITE_HIT_TEST_METRICS caretMetrics{};
        THROW_IF_FAILED(layout.layout()->HitTestTextPosition(static_cast<UINT32>(caret_), FALSE, &caretX, &caretY, &caretMetrics));
        DWRITE_TEXT_METRICS extent{};
        THROW_IF_FAILED(layout.layout()->GetMetrics(&extent));
        if (multiline_) {
            scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, extent.height - innerHeight));
            if (focused_) {
                if (caretY < scroll_) { scroll_ = caretY; }
                else if (caretY + caretMetrics.height > scroll_ + innerHeight) { scroll_ = caretY + caretMetrics.height - innerHeight; }
            }
        } else {
            scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, extent.widthIncludingTrailingWhitespace - innerWidth + 2));
            if (caretX < scroll_) { scroll_ = caretX; }
            else if (caretX > scroll_ + innerWidth - 2) { scroll_ = caretX - innerWidth + 2; }
        }
        const D2D1_POINT_2F origin{padX - (multiline_ ? 0 : scroll_), padY - (multiline_ ? scroll_ : 0)};

        ScopedSurfaceDraw draw{target_.surface(), application().graphics(), dpi()};
        const auto dc = draw.context().get();
        dc->Clear(D2D1::ColorF(surround));
        if (!brush_) { THROW_IF_FAILED(dc->CreateSolidColorBrush(D2D1::ColorF(fill), brush_.put())); }
        const auto bounds = D2D1::RoundedRect({0.5f, 0.5f, size.x - 0.5f, size.y - 0.5f}, 6, 6);
        brush_->SetColor(D2D1::ColorF(fill));
        dc->FillRoundedRectangle(bounds, brush_.get());
        brush_->SetColor(D2D1::ColorF(focused_ ? accent : border));
        dc->DrawRoundedRectangle(bounds, brush_.get(), focused_ ? 1.5f : 1.0f);
        dc->PushAxisAlignedClip({padX - 2, padY, size.x - padX + 2, size.y - padY}, D2D1_ANTIALIAS_MODE_ALIASED);
        if (text_.empty()) {
            TextLayout placeholder{application().graphics().text_factory().get(), placeholder_, fontSize};
            placeholder.resize(innerWidth, innerHeight);
            THROW_IF_FAILED(placeholder.layout()->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
            THROW_IF_FAILED(placeholder.layout()->SetParagraphAlignment(multiline_ ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER));
            brush_->SetColor(D2D1::ColorF(hint));
            dc->DrawTextLayout({padX, padY}, placeholder.layout().get(), brush_.get());
        } else {
            brush_->SetColor(D2D1::ColorF(ink));
            dc->DrawTextLayout(origin, layout.layout().get(), brush_.get());
        }
        dc->PopAxisAlignedClip();
        draw.finish();
        caret_visual_.Offset({origin.x + caretX, origin.y + caretY, 0});
        caret_visual_.Size({1.5f, std::max(4.0f, caretMetrics.height)});
        caret_visual_.IsVisible(focused_);
    });
}

std::optional<LRESULT> TextField::on_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_GETDLGCODE:
        return DLGC_WANTCHARS | DLGC_WANTARROWS | (wparam == VK_RETURN ? DLGC_WANTMESSAGE : 0);
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        return TRUE;
    case WM_LBUTTONDOWN: {
        SetFocus(hwnd());
        const auto scale = static_cast<float>(dpi()) / 96.0f;
        place_caret(GET_X_LPARAM(lparam) / scale - padX + (multiline_ ? 0 : scroll_),
            GET_Y_LPARAM(lparam) / scale - padY + (multiline_ ? scroll_ : 0));
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (!multiline_) { break; }
        scroll_ -= static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA * 48;
        invalidate();
        return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        focused_ = message == WM_SETFOCUS;
        if (focused_) { changed(); }
        else { caret_visual_.IsVisible(false); invalidate(); }
        return 0;
    case WM_CHAR: {
        const auto c = static_cast<wchar_t>(wparam);
        if (c == L'\t' || c >= 0x20) { insert({&c, 1}); }
        return 0;
    }
    case WM_KEYDOWN: {
        const auto word = [&](int direction) {
            std::size_t position = caret_;
            const auto space = [&](std::size_t index) { return std::iswspace(text_[index]) != 0; };
            if (direction < 0) {
                while (position > 0 && space(position - 1)) { --position; }
                while (position > 0 && !space(position - 1)) { --position; }
            } else {
                while (position < text_.size() && !space(position)) { ++position; }
                while (position < text_.size() && space(position)) { ++position; }
            }
            return position;
        };
        switch (wparam) {
        case VK_LEFT: move_caret(control_down() ? word(-1) : step(caret_, -1)); return 0;
        case VK_RIGHT: move_caret(control_down() ? word(1) : step(caret_, 1)); return 0;
        case VK_HOME: {
            std::size_t start = 0;
            if (!control_down() && multiline_ && caret_ > 0) {
                const auto previous = text_.rfind(L'\n', caret_ - 1);
                start = previous == std::wstring::npos ? 0 : previous + 1;
            }
            move_caret(start);
            return 0;
        }
        case VK_END: {
            if (control_down() || !multiline_) { move_caret(text_.size()); return 0; }
            const auto next = text_.find(L'\n', caret_);
            move_caret(next == std::wstring::npos ? text_.size() : next);
            return 0;
        }
        case VK_UP: if (multiline_) { move_line(-1); } return 0;
        case VK_DOWN: if (multiline_) { move_line(1); } return 0;
        case VK_BACK: erase(true, control_down()); return 0;
        case VK_DELETE: erase(false, control_down()); return 0;
        case VK_RETURN:
            if (lparam & (1LL << 30)) { return 0; }
            if (multiline_) { insert(L"\n"); } else { submitted_.emit(); }
            return 0;
        case 'V': if (control_down()) { paste(); return 0; } break;
        }
        break;
    }
    }
    return std::nullopt;
}
