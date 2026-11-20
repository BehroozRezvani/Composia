#include "ElementAutomation.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace composia {
namespace {
HRESULT string_value(std::wstring_view text, VARIANT* out) {
    out->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
    if (!out->bstrVal) {
        return E_OUTOFMEMORY;
    }
    out->vt = VT_BSTR;
    return S_OK;
}
VARIANT boolean(bool b) {
    VARIANT v{};
    v.vt = VT_BOOL;
    v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
    return v;
}
VARIANT number(double d) {
    VARIANT v{};
    v.vt = VT_R8;
    v.dblVal = d;
    return v;
}
using Array = wil::unique_any<SAFEARRAY*, decltype(&SafeArrayDestroy), SafeArrayDestroy>;
HRESULT provider_array(const std::vector<wil::com_ptr<IRawElementProviderSimple>>& providers, SAFEARRAY** out) {
    Array array{SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(providers.size()))};
    if (!array) {
        return E_OUTOFMEMORY;
    }
    for (LONG i = 0; i < static_cast<LONG>(providers.size()); ++i) {
        RETURN_IF_FAILED(SafeArrayPutElement(array.get(), &i, providers[i].get()));
    }
    *out = array.release();
    return S_OK;
}
}

ElementProvider::ElementProvider(Element& e, ElementWindow& w) : element(&e), window(w) {
    static std::atomic<LONG> next{};
    id = --next; // Native Rich Edit site IDs use the positive range.
    last = metrics();
}
ElementProvider::Metrics ElementProvider::metrics() const {
    Metrics m;
    auto b = element->viewport_bounds(true);
    POINT origin{};
    THROW_IF_WIN32_BOOL_FALSE(ClientToScreen(window.hwnd(), &origin));
    const auto scale = window.scale();
    m.rect = {origin.x + double(b.x) * scale, origin.y + double(b.y) * scale, double(b.width) * scale,
              double(b.height) * scale};
    m.offscreen = !IsWindowVisible(window.hwnd()) || b.width <= 0 || b.height <= 0;
    m.enabled = enabled();
    m.focusable = m.enabled && element->focusable();
    if (element->layout_.kind == ElementLayout::Kind::scroll && element->scrollLimit_ > 0) {
        const int axis = element->layout_.axis == layout::Axis::vertical;
        const auto size = axis ? element->bounds().height : element->bounds().width;
        m.percent[axis] = 100.0 * element->scroll_offset() / element->scrollLimit_;
        m.view[axis] = 100.0 * size / (double(size) + element->scrollLimit_);
    }
    return m;
}
void ElementProvider::refresh() {
    if (!element) {
        return;
    }
    auto m = metrics();
    const auto old = std::exchange(last, m);
    if (old.enabled != m.enabled) {
        property(UIA_IsEnabledPropertyId, boolean(old.enabled));
    }
    if (old.focusable != m.focusable) {
        property(UIA_IsKeyboardFocusablePropertyId, boolean(old.focusable));
    }
    if (old.offscreen != m.offscreen) {
        property(UIA_IsOffscreenPropertyId, boolean(old.offscreen));
    }
    if (old.rect.left != m.rect.left || old.rect.top != m.rect.top || old.rect.width != m.rect.width ||
        old.rect.height != m.rect.height) {
        property(UIA_BoundingRectanglePropertyId);
    }
    for (int axis = 0; axis < 2; ++axis) {
        if (m.percent[axis] != old.percent[axis]) {
            property(axis ? UIA_ScrollVerticalScrollPercentPropertyId : UIA_ScrollHorizontalScrollPercentPropertyId,
                     number(old.percent[axis]));
            if ((m.percent[axis] < 0) != (old.percent[axis] < 0)) {
                property(axis ? UIA_ScrollVerticallyScrollablePropertyId
                              : UIA_ScrollHorizontallyScrollablePropertyId,
                         boolean(old.percent[axis] >= 0));
            }
        }
        if (m.view[axis] != old.view[axis]) {
            property(axis ? UIA_ScrollVerticalViewSizePropertyId : UIA_ScrollHorizontalViewSizePropertyId,
                     number(old.view[axis]));
        }
    }
}
void ElementProvider::property(PROPERTYID id, VARIANT previous) noexcept {
    notify_property(*this, id, previous);
}
void ElementProvider::notify_property(IRawElementProviderSimple& provider, PROPERTYID id,
                                      VARIANT previous) noexcept {
    VARIANT now{};
    if (SUCCEEDED(provider.GetPropertyValue(id, &now))) {
        UiaRaiseAutomationPropertyChangedEvent(&provider, id, previous, now);
    }
    VariantClear(&now);
}
HRESULT ElementProvider::actionable() const noexcept {
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return enabled() ? S_OK : UIA_E_ELEMENTNOTENABLED;
}
HRESULT ElementProvider::get_ProviderOptions(ProviderOptions* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
}
HRESULT ElementProvider::GetPatternProvider(PATTERNID pattern, IUnknown** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    try {
        auto& a = info();
        if (pattern == UIA_TogglePatternId && a.toggle) {
            return QueryInterface(__uuidof(IToggleProvider), reinterpret_cast<void**>(out));
        }
        if (pattern == UIA_ExpandCollapsePatternId && a.expansion) {
            return QueryInterface(__uuidof(IExpandCollapseProvider), reinterpret_cast<void**>(out));
        }
        if (pattern == UIA_InvokePatternId && a.invoke) {
            return QueryInterface(__uuidof(IInvokeProvider), reinterpret_cast<void**>(out));
        }
        if (pattern == UIA_ValuePatternId && std::holds_alternative<ElementAccessibility::Value>(a.value)) {
            return QueryInterface(__uuidof(IValueProvider), reinterpret_cast<void**>(out));
        }
        if (pattern == UIA_RangeValuePatternId &&
            std::holds_alternative<ElementAccessibility::RangeValue>(a.value)) {
            return QueryInterface(__uuidof(IRangeValueProvider), reinterpret_cast<void**>(out));
        }
        if (pattern == UIA_SelectionPatternId && a.selection) {
            return QueryInterface(__uuidof(ISelectionProvider), reinterpret_cast<void**>(out));
        }
        if (pattern == UIA_SelectionItemPatternId && a.selected && ElementAutomation::selection_owner(*element)) {
            return QueryInterface(__uuidof(ISelectionItemProvider), reinterpret_cast<void**>(out));
        }
        if (pattern == UIA_ScrollPatternId && element->layout_.kind == ElementLayout::Kind::scroll) {
            return QueryInterface(__uuidof(IScrollProvider), reinterpret_cast<void**>(out));
        }
        if (pattern == UIA_TextPatternId && a.textProvider) {
            auto get = a.textProvider;
            *out = get().detach();
        }
        return S_OK;
    }
    CATCH_RETURN();
}
HRESULT ElementAutomation::metadata(Element& e, PROPERTYID id, VARIANT* out) {
    const auto& a = *e.accessibility();
    switch (id) {
    case UIA_NamePropertyId:
        return string_value(a.name, out);
    case UIA_AutomationIdPropertyId:
        return string_value(a.automationId, out);
    case UIA_HelpTextPropertyId:
        return string_value(a.helpText, out);
    case UIA_ItemStatusPropertyId:
        return string_value(a.itemStatus, out);
    case UIA_LocalizedControlTypePropertyId:
        return a.localizedControlType.empty() ? S_OK : string_value(a.localizedControlType, out);
    case UIA_LabeledByPropertyId: {
        auto label = a.labeledBy.lock();
        if (label && label->host_window() == e.host_window()) {
            if (auto p = ElementAutomation::simple(*label)) {
                out->vt = VT_UNKNOWN;
                out->punkVal = p.detach();
            }
        }
        return S_OK;
    }
    case UIA_DescribedByPropertyId:
    case UIA_ControllerForPropertyId: {
        const auto& links = id == UIA_DescribedByPropertyId ? a.describedBy : a.controllerFor;
        std::vector<wil::com_ptr<IRawElementProviderSimple>> targets;
        for (const auto& link : links) {
            if (auto target = link.lock(); target && target->host_window() == e.host_window()) {
                if (auto p = ElementAutomation::simple(*target)) {
                    targets.push_back(std::move(p));
                }
            }
        }
        if (!targets.empty()) {
            RETURN_IF_FAILED(provider_array(targets, &out->parray));
            out->vt = VT_ARRAY | VT_UNKNOWN;
        }
        return S_OK;
    }
    default:
        return S_FALSE;
    }
}
HRESULT ElementProvider::GetPropertyValue(PROPERTYID id, VARIANT* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    VariantInit(out);
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    try {
        const auto& a = info();
        const HRESULT common = ElementAutomation::metadata(*element, id, out);
        if (common != S_FALSE) {
            return common;
        }
        switch (id) {
        case UIA_ToggleToggleStatePropertyId:
            if (a.toggle) { out->vt = VT_I4; out->lVal = a.toggle->on ? ToggleState_On : ToggleState_Off; }
            return S_OK;
        case UIA_ExpandCollapseExpandCollapseStatePropertyId:
            if (a.expansion) { out->vt = VT_I4; out->lVal = a.expansion->on ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed; }
            return S_OK;
        case UIA_FrameworkIdPropertyId:
            return string_value(L"Composia", out);
        case UIA_ControlTypePropertyId:
            out->vt = VT_I4;
            out->lVal = a.controlType;
            return S_OK;
        case UIA_IsContentElementPropertyId:
        case UIA_IsControlElementPropertyId:
            *out = boolean(true);
            return S_OK;
        case UIA_IsEnabledPropertyId:
            *out = boolean(enabled());
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            *out = boolean(enabled() && element->focusable());
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId:
            *out = boolean(window.focused() && element->focused());
            return S_OK;
        case UIA_SelectionItemIsSelectedPropertyId:
            if (a.selected) {
                *out = boolean(*a.selected);
            }
            return S_OK;
        case UIA_RangeValueValuePropertyId:
        case UIA_RangeValueMaximumPropertyId:
        case UIA_RangeValueMinimumPropertyId:
        case UIA_RangeValueLargeChangePropertyId:
        case UIA_RangeValueSmallChangePropertyId: {
            if (auto r = std::get_if<ElementAccessibility::RangeValue>(&a.value)) {
                double v = r->value;
                if (id == UIA_RangeValueMaximumPropertyId) {
                    v = r->maximum;
                }
                if (id == UIA_RangeValueMinimumPropertyId) {
                    v = r->minimum;
                }
                if (id == UIA_RangeValueLargeChangePropertyId) {
                    v = r->largeChange;
                }
                if (id == UIA_RangeValueSmallChangePropertyId) {
                    v = r->smallChange;
                }
                *out = number(v);
            }
            return S_OK;
        }
        case UIA_RangeValueIsReadOnlyPropertyId:
            if (auto r = std::get_if<ElementAccessibility::RangeValue>(&a.value)) {
                *out = boolean(!r->set);
            }
            return S_OK;
        case UIA_ValueIsReadOnlyPropertyId:
            if (auto v = std::get_if<ElementAccessibility::Value>(&a.value)) {
                *out = boolean(!v->set);
            }
            return S_OK;
        case UIA_SelectionCanSelectMultiplePropertyId:
            if (a.selection) {
                *out = boolean(a.selection->multiple);
            }
            return S_OK;
        case UIA_SelectionIsSelectionRequiredPropertyId:
            if (a.selection) {
                *out = boolean(a.selection->required);
            }
            return S_OK;
        case UIA_ValueValuePropertyId:
            if (auto v = std::get_if<ElementAccessibility::Value>(&a.value)) {
                return string_value(v->text, out);
            }
            return S_OK;
        case UIA_IsOffscreenPropertyId:
        case UIA_BoundingRectanglePropertyId:
        case UIA_ScrollHorizontalScrollPercentPropertyId:
        case UIA_ScrollVerticalScrollPercentPropertyId:
        case UIA_ScrollHorizontalViewSizePropertyId:
        case UIA_ScrollVerticalViewSizePropertyId:
        case UIA_ScrollHorizontallyScrollablePropertyId:
        case UIA_ScrollVerticallyScrollablePropertyId: {
            window.elements().update_layout();
            auto m = metrics();
            if (id == UIA_IsOffscreenPropertyId) {
                *out = boolean(m.offscreen);
            } else if (id == UIA_BoundingRectanglePropertyId) {
                auto* array = SafeArrayCreateVector(VT_R8, 0, 4);
                if (!array) {
                    return E_OUTOFMEMORY;
                }
                auto p = static_cast<double*>(array->pvData);
                p[0] = m.rect.left;
                p[1] = m.rect.top;
                p[2] = m.rect.width;
                p[3] = m.rect.height;
                out->vt = VT_ARRAY | VT_R8;
                out->parray = array;
            } else if (id == UIA_ScrollHorizontalScrollPercentPropertyId ||
                       id == UIA_ScrollVerticalScrollPercentPropertyId) {
                *out = number(m.percent[id == UIA_ScrollVerticalScrollPercentPropertyId]);
            } else if (id == UIA_ScrollHorizontalViewSizePropertyId || id == UIA_ScrollVerticalViewSizePropertyId) {
                *out = number(m.view[id == UIA_ScrollVerticalViewSizePropertyId]);
            } else {
                *out = boolean(m.percent[id == UIA_ScrollVerticallyScrollablePropertyId] >= 0);
            }
            return S_OK;
        }
        default:
            return S_OK;
        }
    }
    CATCH_RETURN();
}
HRESULT ElementProvider::get_HostRawElementProvider(IRawElementProviderSimple** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    return element ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT ElementProvider::Navigate(NavigateDirection dir, IRawElementProviderFragment** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    return element ? ElementAutomation::navigate(window, element, dir, out) : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT ElementProvider::GetRuntimeId(SAFEARRAY** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    auto array = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!array) {
        return E_OUTOFMEMORY;
    }
    auto data = static_cast<LONG*>(array->pvData);
    data[0] = UiaAppendRuntimeId;
    data[1] = id;
    *out = array;
    return S_OK;
}
HRESULT ElementProvider::get_BoundingRectangle(UiaRect* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = {};
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    try {
        window.elements().update_layout();
        *out = metrics().rect;
        return S_OK;
    }
    CATCH_RETURN();
}
HRESULT ElementProvider::GetEmbeddedFragmentRoots(SAFEARRAY** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    return element ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT ElementProvider::SetFocus() noexcept {
    RETURN_IF_FAILED(actionable());
    try {
        return window.focus_element(*element) ? S_OK : UIA_E_INVALIDOPERATION;
    }
    CATCH_RETURN();
}
HRESULT ElementProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    if (!element || !window.automation_provider()) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    return window.automation_provider()->QueryInterface(IID_PPV_ARGS(out));
}
HRESULT ElementProvider::GetSelection(SAFEARRAY** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (!info().selection) {
        return UIA_E_NOTSUPPORTED;
    }
    try {
        std::vector<wil::com_ptr<IRawElementProviderSimple>> peers;
        for (auto* item : ElementAutomation::selected_items(*element)) {
            peers.push_back(ElementAutomation::simple(*item));
        }
        return provider_array(peers, out);
    }
    CATCH_RETURN();
}
}
