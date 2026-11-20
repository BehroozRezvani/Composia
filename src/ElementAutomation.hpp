#pragma once

#include <composia/ElementWindow.hpp>
#include <composia/ElementAccessibility.hpp>
#include <unordered_map>
#include "ElementProvider.hpp"

namespace composia {

// A UI-thread fragment root. UI Automation marshals requests to the owning STA.
struct ElementAutomation
    : winrt::implements<ElementAutomation, IRawElementProviderSimple, IRawElementProviderFragment,
                        IRawElementProviderFragmentRoot, winrt::non_agile> {
    explicit ElementAutomation(ElementWindow& w) : window(&w) {}
    static void attach(ElementWindow&);
    static void disconnect(ElementWindow&) noexcept;
    static bool exposed(Element&) noexcept;
    static Element* first(Element&, bool last) noexcept;
    static Element* neighbor(ElementWindow&, Element*, NavigateDirection) noexcept;
    static HRESULT navigate(ElementWindow&, Element*, NavigateDirection, IRawElementProviderFragment**) noexcept;
    static HRESULT fragment(Element*, IRawElementProviderFragment**) noexcept;
    static wil::com_ptr<IRawElementProviderSimple> simple(Element&);
    static void detach(Element&) noexcept;
    static void structure(Element&, bool hostRoot = false) noexcept;
    static void focus_changed(Element&, bool focused) noexcept;
    static void properties(Element&, const ElementAccessibility* old);
    static void value_changed(Element&, PROPERTYID);
    static HRESULT metadata(Element&, PROPERTYID, VARIANT*);
    static void geometry_changed(Element&) noexcept;
    static void refresh(ElementWindow&) noexcept;
    static Element* selection_owner(Element&) noexcept;
    static std::vector<Element*> selected_items(Element&);
    static void reconcile_selection(Element& parent, Element* added = nullptr);
    std::unordered_map<Element*, winrt::com_ptr<ElementProvider>> nodes;
    ElementWindow* window;
    bool geometryDirty{};

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
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double, double,
                                                       IRawElementProviderFragment**) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment**) noexcept override;
};
}
