#pragma once
#include <composia/ElementWindow.hpp>
#include <composia/ElementAccessibility.hpp>

namespace composia {
struct ElementProvider : winrt::implements<ElementProvider, IRawElementProviderSimple, IRawElementProviderFragment,
                                           IInvokeProvider, IValueProvider, IRangeValueProvider, ISelectionProvider,
                                           ISelectionItemProvider, IScrollProvider, winrt::non_agile> {
    explicit ElementProvider(Element&, ElementWindow&);
    Element* element;
    ElementWindow& window; // Only accessed while element is connected.
    LONG id;
    const ElementAccessibility& info() const { return *element->accessibility(); }
    bool enabled() const { return element->enabled() && window.enabled(); }
    HRESULT actionable() const noexcept;
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions*) noexcept override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown**) noexcept override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID, VARIANT*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple**) noexcept override;
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection, IRawElementProviderFragment**) noexcept override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY**) noexcept override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect*) noexcept override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY**) noexcept override;
    HRESULT STDMETHODCALLTYPE SetFocus() noexcept override;
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot**) noexcept override;

    HRESULT STDMETHODCALLTYPE Invoke() noexcept override;
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR) noexcept override;
    HRESULT STDMETHODCALLTYPE get_Value(BSTR*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL*) noexcept override;
    HRESULT STDMETHODCALLTYPE SetValue(double) noexcept override;
    HRESULT STDMETHODCALLTYPE get_Value(double*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_Maximum(double*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_Minimum(double*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_LargeChange(double*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_SmallChange(double*) noexcept override;
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY**) noexcept override;
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL*) noexcept override;
    HRESULT STDMETHODCALLTYPE Select() noexcept override;
    HRESULT STDMETHODCALLTYPE AddToSelection() noexcept override;
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() noexcept override;
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple**) noexcept override;
    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) noexcept override;
    HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) noexcept override;
    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double*) noexcept override;
    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double*) noexcept override;

    struct Metrics {
        UiaRect rect{};
        double percent[2]{-1, -1}, view[2]{100, 100};
        bool enabled{}, focusable{}, offscreen{};
    };
    Metrics metrics() const;
    Metrics last;
    void refresh();
    void property(PROPERTYID, VARIANT previous = {}) noexcept;
    static void notify_property(IRawElementProviderSimple&, PROPERTYID, VARIANT previous = {}) noexcept;

private:
    HRESULT range_property(double*, double ElementAccessibility::RangeValue::*) noexcept;
    HRESULT selection_action(SelectionAction) noexcept;
    HRESULT scroll_value(bool vertical, bool viewSize, double*) noexcept;
};
}
