#include <composia/TextLayout.hpp>
#include <cmath>
#include <limits>
#include <string>

namespace composia {

TextLayout::TextLayout(IDWriteFactory7* factory, std::wstring_view text, float fontSize, DWRITE_FONT_WEIGHT weight,
    std::wstring_view family, std::wstring_view locale) {
    THROW_HR_IF(E_INVALIDARG, !factory || !std::isfinite(fontSize) || fontSize <= 0 || text.size() > UINT32_MAX || family.empty());
    const std::wstring ownedFamily{family}, ownedLocale{locale};
    THROW_IF_FAILED(factory->CreateTextFormat(ownedFamily.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, fontSize, ownedLocale.c_str(), format_.put()));
    THROW_IF_FAILED(factory->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), format_.get(), 1, 1, layout_.put()));
    width_ = height_ = 1;
}

void TextLayout::resize(float width, float height) {
    THROW_HR_IF(E_INVALIDARG, !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0);
    if (width != width_) { THROW_IF_FAILED(layout_->SetMaxWidth(width)); width_ = width; }
    if (height != height_) { THROW_IF_FAILED(layout_->SetMaxHeight(height)); height_ = height; }
}

DWRITE_TEXT_METRICS TextLayout::metrics() const {
    DWRITE_TEXT_METRICS result{};
    THROW_IF_FAILED(layout_->GetMetrics(&result));
    return result;
}

}
