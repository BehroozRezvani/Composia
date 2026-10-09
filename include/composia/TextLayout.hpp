#pragma once

#include <composia/Native.hpp>
#include <dwrite_3.h>
#include <string_view>

namespace composia {

// A DirectWrite text layout kept across paints, so text is shaped once rather than on every frame.
// Draw it with ID2D1DeviceContext::DrawTextLayout(origin, layout().get(), brush). It needs only the
// DirectWrite factory, so it survives graphics device replacement.
class TextLayout {
public:
    // Sizes are in DIPs. The family and locale default to Segoe UI and en-US. The layout starts
    // 1 by 1 DIP; call resize before measuring or drawing.
    TextLayout(IDWriteFactory7*, std::wstring_view text, float fontSize,
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL,
        std::wstring_view family = L"Segoe UI", std::wstring_view locale = L"en-US");
    // Sets the box the text wraps and aligns in, in DIPs; unchanged dimensions cost nothing.
    void resize(float width, float height);
    // Measures the text as laid out in the current bounds, in DIPs: width and height cover the
    // formatted lines, and lineCount reports wrapping. Call resize first to wrap at a width.
    [[nodiscard]] DWRITE_TEXT_METRICS metrics() const;
    [[nodiscard]] const wil::com_ptr<IDWriteTextLayout>& layout() const noexcept { return layout_; }
    [[nodiscard]] const wil::com_ptr<IDWriteTextFormat>& format() const noexcept { return format_; }

private:
    wil::com_ptr<IDWriteTextFormat> format_;
    wil::com_ptr<IDWriteTextLayout> layout_;
    float width_{};
    float height_{};
};

}
