#include "TextBoxAutomation.hpp"
#include "TextBoxHost.hpp"
#include <algorithm>

namespace composia {

void TextBoxAutomation::attach(ElementWindow& w) {
    if (!w.automation_provider() && editor(&w.elements().root(), false)) {
        if (!w.editorAutomation_) { w.editorAutomation_ = winrt::make_self<TextBoxAutomation>(w); }
        w.set_automation_provider(w.editorAutomation_.get());
    }
}
void TextBoxAutomation::disconnect(ElementWindow& w) noexcept {
    if (auto root = std::move(w.editorAutomation_)) {
        root->window = nullptr;
        UiaDisconnectProvider(root.get());
        if (w.automation_provider() == root.get()) { w.set_automation_provider(nullptr); }
    }
}
TextBox* TextBoxAutomation::editor(Element* e, bool backwards) noexcept {
    for (; e; e = e->adjacent(backwards)) {
        if (auto* box = dynamic_cast<TextBox*>(e)) { return box; }
    }
    return nullptr;
}
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
HRESULT TextBoxAutomation::fragment(TextBox* box, IRawElementProviderFragment** out) noexcept {
    if (!out) { return E_POINTER; }
    *out = nullptr;
    if (!box) { return S_OK; }
    try {
        return box->host_->automation_provider()->QueryInterface(IID_PPV_ARGS(out));
    }
    CATCH_RETURN();
}
HRESULT TextBoxAutomation::get_ProviderOptions(ProviderOptions* out) noexcept {
    if (!out) { return E_POINTER; }
    *out = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
}
HRESULT TextBoxAutomation::GetPatternProvider(PATTERNID, IUnknown** out) noexcept {
    if (!out) { return E_POINTER; }
    *out = nullptr;
    return window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT TextBoxAutomation::GetPropertyValue(PROPERTYID, VARIANT* out) noexcept {
    if (!out) { return E_POINTER; }
    VariantInit(out);
    return window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT TextBoxAutomation::get_HostRawElementProvider(IRawElementProviderSimple** out) noexcept {
    if (!out) { return E_POINTER; }
    *out = nullptr;
    return window ? UiaHostProviderFromHwnd(window->hwnd(), out) : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT TextBoxAutomation::Navigate(NavigateDirection dir, IRawElementProviderFragment** out) noexcept {
    if (!out) { return E_POINTER; }
    *out = nullptr;
    if (!window) { return UIA_E_ELEMENTNOTAVAILABLE; }
    auto& root = window->elements().root();
    if (dir == NavigateDirection_FirstChild) { return fragment(editor(&root, false), out); }
    if (dir == NavigateDirection_LastChild) { return fragment(editor(root.last_descendant(), true), out); }
    return S_OK;
}
HRESULT TextBoxAutomation::GetRuntimeId(SAFEARRAY** out) noexcept {
    if (!out) { return E_POINTER; }
    *out = nullptr;
    return window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT TextBoxAutomation::GetEmbeddedFragmentRoots(SAFEARRAY** out) noexcept {
    return GetRuntimeId(out);
}
HRESULT TextBoxAutomation::get_BoundingRectangle(UiaRect* out) noexcept {
    if (!out) { return E_POINTER; }
    *out = {};
    RECT r{};
    if (!window || !GetWindowRect(window->hwnd(), &r)) { return UIA_E_ELEMENTNOTAVAILABLE; }
    *out = {double(r.left), double(r.top), double(r.right - r.left), double(r.bottom - r.top)};
    return S_OK;
}
HRESULT TextBoxAutomation::SetFocus() noexcept {
    if (!window) { return UIA_E_ELEMENTNOTAVAILABLE; }
    try {
        window->focus();
        return S_OK;
    }
    CATCH_RETURN();
}
HRESULT TextBoxAutomation::get_FragmentRoot(IRawElementProviderFragmentRoot** out) noexcept {
    if (!out) { return E_POINTER; }
    *out = nullptr;
    return window ? QueryInterface(IID_PPV_ARGS(out)) : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT TextBoxAutomation::ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) noexcept {
    if (!out) { return E_POINTER; }
    *out = nullptr;
    if (!window) { return UIA_E_ELEMENTNOTAVAILABLE; }
    POINT p{LONG(x), LONG(y)};
    if (!ScreenToClient(window->hwnd(), &p)) { return UIA_E_ELEMENTNOTAVAILABLE; }
    try {
        for (auto* e = window->elements().hit_test(window->to_dips(p)); e; e = e->parent()) {
            if (auto* box = dynamic_cast<TextBox*>(e)) { return fragment(box, out); }
        }
        return QueryInterface(IID_PPV_ARGS(out));
    }
    CATCH_RETURN();
}
HRESULT TextBoxAutomation::GetFocus(IRawElementProviderFragment** out) noexcept {
    if (!out) { return E_POINTER; }
    *out = nullptr;
    return window ? fragment(dynamic_cast<TextBox*>(window->elements().focused()), out) : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT TextBox::Host::GetAdjacentFragment(NavigateDirection dir, IRawElementProviderFragment** out) noexcept {
    if (!out) { return E_POINTER; }
    *out = nullptr;
    if (!hwnd()) { return UIA_E_ELEMENTNOTAVAILABLE; }
    if (dir == NavigateDirection_Parent) {
        auto* root = box->host_window()->automation_provider();
        return root ? root->QueryInterface(IID_PPV_ARGS(out)) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (dir == NavigateDirection_NextSibling || dir == NavigateDirection_PreviousSibling) {
        const bool backwards = dir == NavigateDirection_PreviousSibling;
        return TextBoxAutomation::fragment(TextBoxAutomation::sibling(*box, backwards), out);
    }
    return S_OK;
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
        out->vt = VT_BSTR;
        out->bstrVal = SysAllocString(box->options().name.c_str());
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
