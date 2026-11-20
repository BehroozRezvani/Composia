#pragma once

#include <composia/ElementWindow.hpp>
#include <UIAutomation.h>

namespace composia {
class TextBox;

// A UI-thread fragment root. UI Automation marshals requests to the owning STA.
struct TextBoxAutomation : winrt::implements<TextBoxAutomation, IRawElementProviderSimple, IRawElementProviderFragment,
                                             IRawElementProviderFragmentRoot, winrt::non_agile> {
    explicit TextBoxAutomation(ElementWindow& w) : window(&w) {}
    static void attach(ElementWindow&);
    static void disconnect(ElementWindow&) noexcept;
    static HRESULT fragment(TextBox*, IRawElementProviderFragment**) noexcept;
    static TextBox* editor(Element*, bool backwards) noexcept;
    static TextBox* sibling(Element& e, bool backwards) noexcept { return editor(e.adjacent(backwards), backwards); }
    ElementWindow* window;

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
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double, double, IRawElementProviderFragment**) noexcept override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment**) noexcept override;
};
}
