#pragma once

#include <composia/Native.hpp>
#include <dwrite_3.h>
#include <string_view>

namespace composia {

class TextLayout {
public:
    TextLayout(IDWriteFactory7*, std::wstring_view text, float fontSize,
        DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL);
    void resize(float width, float height);
    [[nodiscard]] const wil::com_ptr<IDWriteTextLayout>& layout() const noexcept { return layout_; }
    [[nodiscard]] const wil::com_ptr<IDWriteTextFormat>& format() const noexcept { return format_; }

private:
    wil::com_ptr<IDWriteTextFormat> format_;
    wil::com_ptr<IDWriteTextLayout> layout_;
    float width_{};
    float height_{};
};

}
