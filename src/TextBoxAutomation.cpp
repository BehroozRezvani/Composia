#include "ElementAutomation.hpp"
#include "TextBoxHost.hpp"
#include <algorithm>

namespace composia {
IRawElementProviderSimple* TextBox::Host::automation_provider() {
    THROW_HR_IF(static_cast<HRESULT>(UIA_E_ELEMENTNOTAVAILABLE), !hwnd());
    sync();
    if (!provider) {
        static std::atomic<LONG> nextSite{};
        siteId = ++nextSite;
        const auto factory = engine.query<IRicheditWindowlessAccessibility>();
        THROW_IF_FAILED(factory->CreateProvider(this, provider.put()));
    }
    return provider.get();
}
HRESULT TextBox::Host::GetAdjacentFragment(NavigateDirection dir, IRawElementProviderFragment** out) noexcept {
    if (!out) { return E_POINTER; }
    *out = nullptr;
    return hwnd() ? ElementAutomation::navigate(*box->host_window(), box, dir, out) : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT TextBox::Host::GetRuntimeIdPrefix(SAFEARRAY** out) noexcept {
    if (!out) { return E_POINTER; }
    *out = nullptr;
    if (!hwnd()) { return UIA_E_ELEMENTNOTAVAILABLE; }
    auto* array = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!array) { return E_OUTOFMEMORY; }
    auto* data = static_cast<LONG*>(array->pvData);
    data[0] = UiaAppendRuntimeId;
    data[1] = siteId;
    *out = array;
    return S_OK;
}
HRESULT TextBox::Host::GetPropertyOverrideValue(PROPERTYID property, VARIANT* out) noexcept {
    if (!out) { return E_POINTER; }
    VariantInit(out);
    if (!hwnd()) { return UIA_E_ELEMENTNOTAVAILABLE; }
    if (property == UIA_NamePropertyId) {
        if (box->accessibility() && !box->accessibility()->name.empty()) {
            return ElementAutomation::metadata(*box, property, out);
        }
        out->vt = VT_BSTR;
        out->bstrVal = SysAllocString(box->options().name.c_str());
        return out->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    if (property == UIA_IsPasswordPropertyId) {
        out->vt = VT_BOOL;
        out->boolVal = box->options().passwordChar ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }
    if (property == UIA_ValueValuePropertyId && box->options().passwordChar) {
        // A password's value is not read out.
        out->vt = VT_BSTR;
        out->bstrVal = SysAllocString(L"");
        return out->bstrVal ? S_OK : E_OUTOFMEMORY;
    }
    if (property == UIA_HasKeyboardFocusPropertyId || property == UIA_IsEnabledPropertyId) {
        out->vt = VT_BOOL;
        out->boolVal = (property == UIA_IsEnabledPropertyId ? box->enabled() && box->host_window()->enabled()
                                                            : box->focused() && box->host_window()->focused())
                           ? VARIANT_TRUE
                           : VARIANT_FALSE;
        return S_OK;
    }
    if (box->accessibility()) {
        try { return ElementAutomation::metadata(*box,property,out); } CATCH_RETURN();
    }
    return S_FALSE;
}

std::vector<layout::Rect> TextBox::selection_rectangles() const {
    std::vector<layout::Rect> result;
    const auto selected = selection();
    if (!host_window() || selected.start == selected.end) { return result; }
    // The same native text range drives screen readers and custom painters.
    wil::com_ptr<IUnknown> pattern;
    THROW_IF_FAILED(host_->automation_provider()->GetPatternProvider(UIA_TextPatternId, pattern.put()));
    THROW_HR_IF_NULL(E_NOINTERFACE, pattern);
    using Array = wil::unique_any<SAFEARRAY*, decltype(&SafeArrayDestroy), SafeArrayDestroy>;
    Array ranges;
    THROW_IF_FAILED(pattern.query<ITextProvider>()->GetSelection(ranges.put()));
    if (!ranges || !ranges.get()->rgsabound[0].cElements) { return result; }
    LONG first = ranges.get()->rgsabound[0].lLbound;
    wil::com_ptr<IUnknown> unknown;
    THROW_IF_FAILED(SafeArrayGetElement(ranges.get(), &first, unknown.put()));
    Array rectangles;
    THROW_IF_FAILED(unknown.query<ITextRangeProvider>()->GetBoundingRectangles(rectangles.put()));
    if (!rectangles) { return result; }
    const auto count = rectangles.get()->rgsabound[0].cElements;
    const auto data = static_cast<const double*>(rectangles.get()->pvData);
    POINT origin{};
    THROW_IF_WIN32_BOOL_FALSE(host_->TxClientToScreen(&origin));
    UiaRect clip{};
    THROW_IF_FAILED(host_->GetBoundaryRectangle(&clip));
    const double scale = host_window()->scale();
    result.reserve(count / 4);
    for (ULONG i = 0; i + 3 < count; i += 4) {
        const auto x = std::max(data[i], clip.left), y = std::max(data[i + 1], clip.top);
        const auto right = std::min(data[i] + data[i + 2], clip.left + clip.width);
        const auto bottom = std::min(data[i + 1] + data[i + 3], clip.top + clip.height);
        if (right > x && bottom > y) {
            result.push_back({float((x - origin.x) / scale), float((y - origin.y) / scale), float((right - x) / scale),
                              float((bottom - y) / scale)});
        }
    }
    return result;
}
}
