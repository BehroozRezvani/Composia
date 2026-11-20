#include "ElementAutomation.hpp"
#include "TextBoxHost.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace composia {
void ElementAutomation::attach(ElementWindow& w) {
    if (!w.automation_provider()) {
        if (!w.automation_) {
            w.automation_ = winrt::make_self<ElementAutomation>(w);
        }
        w.set_automation_provider(w.automation_.get());
    }
}
void ElementAutomation::disconnect(ElementWindow& w) noexcept {
    if (auto root = std::move(w.automation_)) {
        root->window = nullptr;
        for (auto& [element, peer] : root->nodes) {
            peer->element = nullptr;
            UiaDisconnectProvider(peer.get());
        }
        root->nodes.clear();
        UiaDisconnectProvider(root.get());
        if (w.automation_provider() == root.get()) {
            w.set_automation_provider(nullptr);
        }
    }
}
bool ElementAutomation::exposed(Element& e) noexcept {
    return e.accessibility() || dynamic_cast<TextBox*>(&e);
}

Element* ElementAutomation::first(Element& e, bool last) noexcept {
    if (exposed(e)) {
        return &e;
    }
    auto children = e.children();
    for (size_t i = 0; i < children.size(); ++i) {
        if (auto* found = first(*children[last ? children.size() - 1 - i : i], last)) {
            return found;
        }
    }
    return nullptr;
}

Element* ElementAutomation::neighbor(ElementWindow& w, Element* e, NavigateDirection dir) noexcept {
    const bool backwards = dir == NavigateDirection_LastChild || dir == NavigateDirection_PreviousSibling;
    if (dir == NavigateDirection_FirstChild || dir == NavigateDirection_LastChild) {
        if (!e) {
            return first(w.elements().root(), backwards);
        }
        const auto children = e->children();
        for (size_t i = 0; i < children.size(); ++i) {
            if (auto* found = first(*children[backwards ? children.size() - 1 - i : i], backwards)) {
                return found;
            }
        }
    } else if (dir == NavigateDirection_Parent) {
        for (auto* p = e ? e->parent() : nullptr; p; p = p->parent()) {
            if (exposed(*p)) {
                return p;
            }
        }
    } else if (dir == NavigateDirection_NextSibling || dir == NavigateDirection_PreviousSibling) {
        for (auto* p = e; p && p->parent(); p = p->parent()) {
            auto siblings = p->parent()->children();
            auto i = p->siblingIndex_;
            while (backwards ? i > 0 : i + 1 < siblings.size()) {
                i = backwards ? i - 1 : i + 1;
                if (auto* found = first(*siblings[i], backwards)) {
                    return found;
                }
            }
            if (exposed(*p->parent())) {
                break;
            }
        }
    }
    return nullptr;
}

wil::com_ptr<IRawElementProviderSimple> ElementAutomation::simple(Element& e) {
    auto* w = e.host_window();
    if (!w || !w->automation_ || w->automation_provider() != w->automation_.get()) {
        return {};
    }
    if (auto* box = dynamic_cast<TextBox*>(&e)) {
        return box->host_->automation_provider();
    }
    if (!e.accessibility()) {
        return {};
    }
    auto& nodes = w->automation_->nodes;
    auto it = nodes.find(&e);
    if (it == nodes.end()) {
        it = nodes.emplace(&e, winrt::make_self<ElementProvider>(e, *w)).first;
    }
    return it->second.get();
}
HRESULT ElementAutomation::fragment(Element* e, IRawElementProviderFragment** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    try {
        auto peer = e ? simple(*e) : nullptr;
        return peer ? peer->QueryInterface(IID_PPV_ARGS(out)) : S_OK;
    }
    CATCH_RETURN();
}
HRESULT ElementAutomation::navigate(ElementWindow& w, Element* from, NavigateDirection dir,
                                    IRawElementProviderFragment** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    auto* next = neighbor(w, from, dir);
    if (!next && from && dir == NavigateDirection_Parent && w.automation_provider()) {
        return w.automation_provider()->QueryInterface(IID_PPV_ARGS(out));
    }
    return fragment(next, out);
}
void ElementAutomation::detach(Element& e) noexcept {
    auto* w = e.host_window();
    if (!w || !w->automation_) {
        return;
    }
    if (auto node = w->automation_->nodes.extract(&e)) {
        node.mapped()->element = nullptr;
        UiaDisconnectProvider(node.mapped().get());
    }
}
void ElementAutomation::structure(Element& e, bool hostRoot) noexcept {
    auto* w = e.host_window();
    if (!w || !w->automation_ || !UiaClientsAreListening()) {
        return;
    }
    try {
        Element* p = hostRoot ? nullptr : &e;
        while (p && !exposed(*p)) {
            p = p->parent();
        }
        auto provider = p ? simple(*p) : wil::com_ptr<IRawElementProviderSimple>(w->automation_.get());
        if (provider) {
            UiaRaiseStructureChangedEvent(provider.get(), StructureChangeType_ChildrenInvalidated, nullptr, 0);
        }
    } catch (...) { LOG_CAUGHT_EXCEPTION(); }
}
void ElementAutomation::focus_changed(Element& e, bool focused) noexcept {
    auto* w = e.host_window();
    if (!w || !exposed(e) || !UiaClientsAreListening()) {
        return;
    }
    try {
        attach(*w);
        if (auto p = simple(e)) {
            VARIANT before{}, after{};
            before.vt = after.vt = VT_BOOL;
            before.boolVal = focused ? VARIANT_FALSE : VARIANT_TRUE;
            after.boolVal = focused ? VARIANT_TRUE : VARIANT_FALSE;
            UiaRaiseAutomationPropertyChangedEvent(p.get(), UIA_HasKeyboardFocusPropertyId, before, after);
            if (focused) {
                UiaRaiseAutomationEvent(p.get(), UIA_AutomationFocusChangedEventId);
            }
        }
    } catch (...) { LOG_CAUGHT_EXCEPTION(); }
}
void ElementAutomation::refresh(ElementWindow& w) noexcept {
    if (!w.automation_ || !std::exchange(w.automation_->geometryDirty, false) || !UiaClientsAreListening()) {
        return;
    }
    try {
        // Publishing an event can reenter the STA and detach another peer.
        std::vector<winrt::com_ptr<ElementProvider>> peers;
        peers.reserve(w.automation_->nodes.size());
        for (auto& entry : w.automation_->nodes) {
            peers.push_back(entry.second);
        }
        for (auto& peer : peers) {
            peer->refresh();
        }
    } catch (...) { LOG_CAUGHT_EXCEPTION(); }
}
HRESULT ElementAutomation::get_ProviderOptions(ProviderOptions* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
    return S_OK;
}
void ElementAutomation::geometry_changed(Element& e) noexcept {
    if (auto* w = e.host_window(); w && w->automation_) {
        w->automation_->geometryDirty = true;
    }
}
HRESULT ElementAutomation::GetPatternProvider(PATTERNID, IUnknown** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    return window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT ElementAutomation::GetPropertyValue(PROPERTYID, VARIANT* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    VariantInit(out);
    return window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT ElementAutomation::get_HostRawElementProvider(IRawElementProviderSimple** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    return window ? UiaHostProviderFromHwnd(window->hwnd(), out) : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT ElementAutomation::Navigate(NavigateDirection dir, IRawElementProviderFragment** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    return window ? navigate(*window, nullptr, dir, out) : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT ElementAutomation::GetRuntimeId(SAFEARRAY** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    return window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT ElementAutomation::GetEmbeddedFragmentRoots(SAFEARRAY** out) noexcept {
    return GetRuntimeId(out);
}
HRESULT ElementAutomation::get_BoundingRectangle(UiaRect* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = {};
    RECT r{};
    if (!window || !GetWindowRect(window->hwnd(), &r)) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    *out = {double(r.left), double(r.top), double(r.right - r.left), double(r.bottom - r.top)};
    return S_OK;
}
HRESULT ElementAutomation::SetFocus() noexcept {
    if (!window) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    try {
        window->focus();
        return S_OK;
    }
    CATCH_RETURN();
}
HRESULT ElementAutomation::get_FragmentRoot(IRawElementProviderFragmentRoot** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    return window ? QueryInterface(IID_PPV_ARGS(out)) : UIA_E_ELEMENTNOTAVAILABLE;
}
HRESULT ElementAutomation::ElementProviderFromPoint(double x, double y,
                                                    IRawElementProviderFragment** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    if (!window) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    POINT p{LONG(x), LONG(y)};
    if (!ScreenToClient(window->hwnd(), &p)) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    try {
        for (auto* e = window->elements().hit_test(window->to_dips(p)); e; e = e->parent()) {
            if (exposed(*e)) {
                return fragment(e, out);
            }
        }
        return QueryInterface(IID_PPV_ARGS(out));
    }
    CATCH_RETURN();
}
HRESULT ElementAutomation::GetFocus(IRawElementProviderFragment** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    return window ? fragment(window->focused() ? window->elements().focused() : nullptr, out)
                  : UIA_E_ELEMENTNOTAVAILABLE;
}
}
