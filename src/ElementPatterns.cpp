#include "ElementAutomation.hpp"
#include <composia/Application.hpp>
#include <composia/TextBox.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace composia {
using Range = ElementAccessibility::RangeValue;
using Value = ElementAccessibility::Value;

namespace {
// Stop at nested selection containers. A visitor can stop early once it finds the active item.
template <class Visitor> bool visit_items(Element& item, Visitor& visit) {
    const auto* state = item.accessibility();
    if (state && state->selected && visit(item)) {
        return true;
    }
    if (state && state->selection) {
        return false;
    }
    for (auto& child : item.children()) {
        if (visit_items(*child, visit)) {
            return true;
        }
    }
    return false;
}
}

bool Element::accessibility_key(const KeyEvent& key) {
    if (!key.down || key.alt || !focused() || !enabled()) {
        return false;
    }
    auto& a = *accessibility_;
    if (a.selected && key.key == VK_SPACE) {
        return select_item(key.control ? (*a.selected ? SelectionAction::remove : SelectionAction::add)
                                       : SelectionAction::select);
    }
    if (key.control || key.shift) {
        return false;
    }
    if (a.invoke && (key.key == VK_RETURN || key.key == VK_SPACE)) {
        auto invoke = a.invoke;
        if (host_window() && UiaClientsAreListening()) {
            if (auto p = ElementAutomation::simple(*this)) {
                UiaRaiseAutomationEvent(p.get(), UIA_Invoke_InvokedEventId);
            }
        }
        invoke();
        return true;
    }
    if (auto* range = std::get_if<Range>(&a.value); range && range->set) {
        double v = range->value;
        switch (key.key) {
        case VK_LEFT:
        case VK_DOWN:
            v -= range->smallChange;
            break;
        case VK_RIGHT:
        case VK_UP:
            v += range->smallChange;
            break;
        case VK_PRIOR:
            v += range->largeChange;
            break;
        case VK_NEXT:
            v -= range->largeChange;
            break;
        case VK_HOME:
            v = range->minimum;
            break;
        case VK_END:
            v = range->maximum;
            break;
        default:
            return false;
        }
        v = std::clamp(v, range->minimum, range->maximum);
        auto set = range->set;
        set(v);
        return true;
    }
    return false;
}

void Element::set_accessibility(ElementAccessibility value) {
    check_mutation();
    if (auto* r = std::get_if<Range>(&value.value);
        r && (!std::isfinite(r->minimum) || !std::isfinite(r->maximum) || !std::isfinite(r->value) ||
              !std::isfinite(r->smallChange) || !std::isfinite(r->largeChange) || r->minimum > r->value ||
              r->value > r->maximum || r->smallChange < 0 || r->largeChange < 0)) {
        throw std::invalid_argument("Invalid accessible range or change increment");
    }
    auto old = std::exchange(accessibility_, std::make_unique<ElementAccessibility>(std::move(value)));
    if (accessibility_->selection) {
        ElementAutomation::reconcile_selection(*this, this);
    }
    if (parent_ &&
        (accessibility_->selected || accessibility_->selection || (old && (old->selected || old->selection)))) {
        ElementAutomation::reconcile_selection(*parent_, this);
    }
    if (!old) {
        ElementAutomation::structure(parent_ ? *parent_ : *this, parent_ == nullptr);
    }
    ElementAutomation::properties(*this, old.get());
    invalidate();
}
void Element::clear_accessibility() {
    check_mutation();
    if (!accessibility_) {
        return;
    }
    ElementAutomation::detach(*this);
    auto old = std::move(accessibility_);
    if (parent_) {
        ElementAutomation::reconcile_selection(*parent_, this);
    }
    ElementAutomation::structure(parent_ ? *parent_ : *this, parent_ == nullptr);
    ElementAutomation::properties(*this, old.get());
    invalidate();
}

void ElementAutomation::value_changed(Element& element, PROPERTYID property) {
    auto* window = element.host_window();
    if (!window || !window->automation_ || !UiaClientsAreListening()) {
        return;
    }
    auto& nodes = window->automation_->nodes;
    if (auto it = nodes.find(&element); it != nodes.end()) {
        auto peer = it->second;
        peer->property(property);
    }
}

void Element::set_accessible_value(double value) {
    check_mutation();
    auto* range = accessibility_ ? std::get_if<Range>(&accessibility_->value) : nullptr;
    if (!range) {
        throw std::logic_error("Element has no RangeValue pattern");
    }
    if (!std::isfinite(value) || value < range->minimum || value > range->maximum) {
        throw std::invalid_argument("Accessible value is outside the range");
    }
    if (value != range->value) {
        range->value = value;
        invalidate();
        ElementAutomation::value_changed(*this, UIA_RangeValueValuePropertyId);
    }
}

void Element::set_accessible_value(std::wstring value) {
    check_mutation();
    auto* state = accessibility_ ? std::get_if<Value>(&accessibility_->value) : nullptr;
    if (!state) {
        throw std::logic_error("Element has no Value pattern");
    }
    if (value != state->text) {
        state->text = std::move(value);
        invalidate();
        ElementAutomation::value_changed(*this, UIA_ValueValuePropertyId);
    }
}
Element* ElementAutomation::selection_owner(Element& e) noexcept {
    for (auto* p = e.parent(); p; p = p->parent()) {
        if (p->accessibility() && p->accessibility()->selection) {
            return p;
        }
    }
    return nullptr;
}
std::vector<Element*> ElementAutomation::selected_items(Element& owner) {
    std::vector<Element*> result;
    auto collect = [&](Element& item) {
        if (*item.accessibility()->selected) {
            result.push_back(&item);
        }
        return false;
    };
    for (auto& child : owner.children()) {
        visit_items(*child, collect);
    }
    return result;
}
void ElementAutomation::reconcile_selection(Element& parent, Element* added) {
    auto* owner = parent.accessibility() && parent.accessibility()->selection ? &parent : selection_owner(parent);
    if (!owner) {
        return;
    }
    const auto& rules = *owner->accessibility()->selection;
    auto isSelected = [](Element& e) { return *e.accessibility()->selected; };
    const bool checkDuplicates = !rules.multiple && added && (added == owner || visit_items(*added, isSelected));
    if (!rules.required && !checkDuplicates) {
        return;
    }
    Element* first = nullptr;
    bool found = false;
    std::vector<Element*> changed;
    auto check = [&](Element& item) {
        if (!first) {
            first = &item;
        }
        if (*item.accessibility_->selected) {
            if (found && checkDuplicates) {
                changed.push_back(&item);
            }
            found = true;
        }
        return found && !checkDuplicates;
    };
    for (auto& child : owner->children()) {
        if (visit_items(*child, check)) {
            break;
        }
    }
    if (!found && first && rules.required) {
        changed.push_back(first);
    }
    for (auto* item : changed) {
        item->accessibility_->selected = !found;
        item->invalidate();
    }
    if (!changed.empty() && owner->host_window() && UiaClientsAreListening()) {
        if (auto peer = simple(*owner)) {
            UiaRaiseAutomationEvent(peer.get(), UIA_Selection_InvalidatedEventId);
        }
    }
}
bool Element::select_item(SelectionAction action) {
    check_mutation();
    auto* owner = ElementAutomation::selection_owner(*this);
    if (!enabled() || !accessibility_ || !accessibility_->selected || !owner) {
        return false;
    }
    const auto& group = *owner->accessibility_->selection;
    const auto keepOwner = tree_ ? tree_->reference(owner) : nullptr;
    auto selected = ElementAutomation::selected_items(*owner);
    const bool before = *accessibility_->selected;
    if (action == SelectionAction::add && !before && !group.multiple && !selected.empty()) {
        return false;
    }
    if (action == SelectionAction::remove && before && group.required && selected.size() == 1) {
        return false;
    }
    auto changed = group.changed;
    // Hold changed items through UIA callbacks, which can reenter and detach a subtree.
    std::vector<std::shared_ptr<Element>> updates;
    const auto include = [&](Element& item) { updates.push_back(item.parent_->children_[item.siblingIndex_]); };
    if (action == SelectionAction::select) {
        for (auto* other : selected) {
            if (other != this) {
                include(*other);
            }
        }
    }
    if (before != (action != SelectionAction::remove)) {
        include(*this);
    }
    for (const auto& e : updates) {
        e->accessibility_->selected = e.get() == this && action != SelectionAction::remove;
    }
    for (const auto& e : updates) {
        e->invalidate();
    }
    if (updates.empty()) {
        return true;
    }
    if (host_window() && UiaClientsAreListening()) {
        for (const auto& e : updates) {
            if (auto peer = ElementAutomation::simple(*e)) {
                VARIANT old{}, now{};
                old.vt = now.vt = VT_BOOL;
                const bool on = e.get() == this && action != SelectionAction::remove;
                now.boolVal = on ? VARIANT_TRUE : VARIANT_FALSE;
                old.boolVal = on ? VARIANT_FALSE : VARIANT_TRUE;
                UiaRaiseAutomationPropertyChangedEvent(peer.get(), UIA_SelectionItemIsSelectedPropertyId, old, now);
                UiaRaiseAutomationEvent(peer.get(), on ? (action == SelectionAction::select
                                                              ? UIA_SelectionItem_ElementSelectedEventId
                                                              : UIA_SelectionItem_ElementAddedToSelectionEventId)
                                                       : UIA_SelectionItem_ElementRemovedFromSelectionEventId);
            }
        }
    }
    if (changed) {
        changed();
    }
    return true;
}
void ElementAutomation::properties(Element& e, const ElementAccessibility* old) {
    if (!e.host_window() || !e.host_window()->automation_ || !UiaClientsAreListening()) {
        return;
    }
    // Rich Edit remains attached when metadata is added or cleared. Publish the native
    // provider's effective value so clients see its fallback name when an override clears.
    if ((!old || !e.accessibility()) && !dynamic_cast<TextBox*>(&e)) {
        return;
    }
    auto peer = simple(e);
    if (!peer) {
        return;
    }
    const ElementAccessibility defaults;
    if (!old) {
        old = &defaults;
    }
    std::vector<PROPERTYID> changed;
    const auto& a = e.accessibility() ? *e.accessibility() : defaults;
    if (a.toggle && (!old->toggle || old->toggle->on != a.toggle->on)) {
        changed.push_back(UIA_ToggleToggleStatePropertyId);
    }
    if (a.expansion && (!old->expansion || old->expansion->on != a.expansion->on)) {
        changed.push_back(UIA_ExpandCollapseExpandCollapseStatePropertyId);
    }
    if (old->name != a.name) {
        changed.push_back(UIA_NamePropertyId);
    }
    if (old->itemStatus != a.itemStatus) {
        changed.push_back(UIA_ItemStatusPropertyId);
    }
    if (old->helpText != a.helpText) {
        changed.push_back(UIA_HelpTextPropertyId);
    }
    if (old->localizedControlType != a.localizedControlType) {
        changed.push_back(UIA_LocalizedControlTypePropertyId);
    }
    if (old->controlType != a.controlType) {
        changed.push_back(UIA_ControlTypePropertyId);
    }
    if (old->automationId != a.automationId) {
        changed.push_back(UIA_AutomationIdPropertyId);
    }
    if (old->selected != a.selected) {
        changed.push_back(UIA_SelectionItemIsSelectedPropertyId);
    }
    if (old->selection && a.selection) {
        if (old->selection->multiple != a.selection->multiple) {
            changed.push_back(UIA_SelectionCanSelectMultiplePropertyId);
        }
        if (old->selection->required != a.selection->required) {
            changed.push_back(UIA_SelectionIsSelectionRequiredPropertyId);
        }
    }
    const auto sameOwner = [](const auto& lhs, const auto& rhs) {
        return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
    };
    if (!sameOwner(old->labeledBy, a.labeledBy)) {
        changed.push_back(UIA_LabeledByPropertyId);
    }
    if (!std::ranges::equal(old->describedBy, a.describedBy, sameOwner)) {
        changed.push_back(UIA_DescribedByPropertyId);
    }
    if (!std::ranges::equal(old->controllerFor, a.controllerFor, sameOwner)) {
        changed.push_back(UIA_ControllerForPropertyId);
    }
    const auto* r = std::get_if<Range>(&a.value);
    const auto* previous = std::get_if<Range>(&old->value);
    if (r && (!previous || r->value != previous->value)) {
        changed.push_back(UIA_RangeValueValuePropertyId);
    }
    if (r && previous) {
        if (r->minimum != previous->minimum) {
            changed.push_back(UIA_RangeValueMinimumPropertyId);
        }
        if (r->maximum != previous->maximum) {
            changed.push_back(UIA_RangeValueMaximumPropertyId);
        }
        if (r->smallChange != previous->smallChange) {
            changed.push_back(UIA_RangeValueSmallChangePropertyId);
        }
        if (r->largeChange != previous->largeChange) {
            changed.push_back(UIA_RangeValueLargeChangePropertyId);
        }
        if (bool(r->set) != bool(previous->set)) {
            changed.push_back(UIA_RangeValueIsReadOnlyPropertyId);
        }
    }
    auto* v = std::get_if<Value>(&a.value);
    auto* ov = std::get_if<Value>(&old->value);
    if (v && (!ov || v->text != ov->text)) {
        changed.push_back(UIA_ValueValuePropertyId);
    }
    if (v && ov && bool(v->set) != bool(ov->set)) {
        changed.push_back(UIA_ValueIsReadOnlyPropertyId);
    }
    for (auto property : changed) {
        ElementProvider::notify_property(*peer.get(), property);
    }
}

HRESULT ElementProvider::set_switch(const std::optional<ElementAccessibility::Switch>& state, bool on) noexcept {
    if (!state || !state->set) { return UIA_E_NOTSUPPORTED; }
    try {
        const auto target = window.elements().reference(element);
        if (state->on != on) { auto set = state->set; set(on); }
        return S_OK;
    } CATCH_RETURN();
}
HRESULT ElementProvider::Toggle() noexcept {
    RETURN_IF_FAILED(actionable());
    return set_switch(info().toggle, info().toggle && !info().toggle->on);
}
HRESULT ElementProvider::Expand() noexcept {
    RETURN_IF_FAILED(actionable());
    return set_switch(info().expansion, true);
}
HRESULT ElementProvider::Collapse() noexcept {
    RETURN_IF_FAILED(actionable());
    return set_switch(info().expansion, false);
}
HRESULT ElementProvider::get_ToggleState(ToggleState* out) noexcept {
    if (!out) { return E_POINTER; }
    if (!element) { return UIA_E_ELEMENTNOTAVAILABLE; }
    if (!info().toggle) { return UIA_E_NOTSUPPORTED; }
    *out = info().toggle->on ? ToggleState_On : ToggleState_Off;
    return S_OK;
}
HRESULT ElementProvider::get_ExpandCollapseState(ExpandCollapseState* out) noexcept {
    if (!out) { return E_POINTER; }
    if (!element) { return UIA_E_ELEMENTNOTAVAILABLE; }
    if (!info().expansion) { return UIA_E_NOTSUPPORTED; }
    *out = info().expansion->on ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed;
    return S_OK;
}
HRESULT ElementProvider::Invoke() noexcept {
    RETURN_IF_FAILED(actionable());
    if (!info().invoke) {
        return UIA_E_NOTSUPPORTED;
    }
    try {
        return window.application().post([self = get_strong()] {
            if (SUCCEEDED(self->actionable())) {
                const auto target = self->window.elements().reference(self->element);
                if (self->info().invoke) {
                    UiaRaiseAutomationEvent(self.get(), UIA_Invoke_InvokedEventId);
                    if (SUCCEEDED(self->actionable())) {
                        if (auto invoke = self->info().invoke) { invoke(); }
                    }
                }
            }
        })
                   ? S_OK
                   : UIA_E_ELEMENTNOTAVAILABLE;
    }
    CATCH_RETURN();
}
HRESULT ElementProvider::SetValue(LPCWSTR value) noexcept {
    if (!value) {
        return E_INVALIDARG;
    }
    RETURN_IF_FAILED(actionable());
    auto* v = std::get_if<Value>(&info().value);
    if (!v) {
        return UIA_E_NOTSUPPORTED;
    }
    if (!v->set) {
        return UIA_E_INVALIDOPERATION;
    }
    try {
        auto set = v->set;
        const auto target = window.elements().reference(element);
        set(value);
        return S_OK;
    }
    CATCH_RETURN();
}
HRESULT ElementProvider::get_Value(BSTR* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    const auto* v = std::get_if<Value>(&info().value);
    if (!v) {
        return UIA_E_NOTSUPPORTED;
    }
    *out = SysAllocStringLen(v->text.data(), static_cast<UINT>(v->text.size()));
    return *out ? S_OK : E_OUTOFMEMORY;
}
HRESULT ElementProvider::get_IsReadOnly(BOOL* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (auto* r = std::get_if<Range>(&info().value)) {
        *out = !r->set;
        return S_OK;
    }
    if (auto* v = std::get_if<Value>(&info().value)) {
        *out = !v->set;
        return S_OK;
    }
    return UIA_E_NOTSUPPORTED;
}
HRESULT ElementProvider::SetValue(double value) noexcept {
    RETURN_IF_FAILED(actionable());
    auto* r = std::get_if<Range>(&info().value);
    if (!r) {
        return UIA_E_NOTSUPPORTED;
    }
    if (!r->set) {
        return UIA_E_INVALIDOPERATION;
    }
    if (!std::isfinite(value) || value < r->minimum || value > r->maximum) {
        return E_INVALIDARG;
    }
    try {
        auto set = r->set;
        const auto target = window.elements().reference(element);
        set(value);
        return S_OK;
    }
    CATCH_RETURN();
}
HRESULT ElementProvider::range_property(double* out, double Range::* field) noexcept {
    if (!out) {
        return E_POINTER;
    }
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (auto* r = std::get_if<Range>(&info().value)) {
        *out = r->*field;
        return S_OK;
    }
    return UIA_E_NOTSUPPORTED;
}
HRESULT ElementProvider::get_Value(double* out) noexcept {
    return range_property(out, &Range::value);
}
HRESULT ElementProvider::get_Maximum(double* out) noexcept {
    return range_property(out, &Range::maximum);
}
HRESULT ElementProvider::get_Minimum(double* out) noexcept {
    return range_property(out, &Range::minimum);
}
HRESULT ElementProvider::get_LargeChange(double* out) noexcept {
    return range_property(out, &Range::largeChange);
}
HRESULT ElementProvider::get_SmallChange(double* out) noexcept {
    return range_property(out, &Range::smallChange);
}
HRESULT ElementProvider::get_CanSelectMultiple(BOOL* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (!info().selection) {
        return UIA_E_NOTSUPPORTED;
    }
    *out = info().selection->multiple;
    return S_OK;
}
HRESULT ElementProvider::get_IsSelectionRequired(BOOL* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (!info().selection) {
        return UIA_E_NOTSUPPORTED;
    }
    *out = info().selection->required;
    return S_OK;
}
HRESULT ElementProvider::selection_action(SelectionAction action) noexcept {
    RETURN_IF_FAILED(actionable());
    try {
        return element->select_item(action) ? S_OK : UIA_E_INVALIDOPERATION;
    }
    CATCH_RETURN();
}
HRESULT ElementProvider::Select() noexcept {
    return selection_action(SelectionAction::select);
}
HRESULT ElementProvider::AddToSelection() noexcept {
    return selection_action(SelectionAction::add);
}
HRESULT ElementProvider::RemoveFromSelection() noexcept {
    return selection_action(SelectionAction::remove);
}
HRESULT ElementProvider::get_IsSelected(BOOL* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (!info().selected) {
        return UIA_E_NOTSUPPORTED;
    }
    *out = *info().selected;
    return S_OK;
}
HRESULT ElementProvider::get_SelectionContainer(IRawElementProviderSimple** out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    *out = nullptr;
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    try {
        if (auto owner = ElementAutomation::selection_owner(*element)) {
            *out = ElementAutomation::simple(*owner).detach();
        }
        return S_OK;
    }
    CATCH_RETURN();
}

HRESULT ElementProvider::scroll_value(bool vertical, bool viewSize, double* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    if (!element) {
        return UIA_E_ELEMENTNOTAVAILABLE;
    }
    if (element->layout_.kind != ElementLayout::Kind::scroll) {
        return UIA_E_NOTSUPPORTED;
    }
    try {
        window.elements().update_layout();
        auto m = metrics();
        *out = viewSize ? m.view[vertical] : m.percent[vertical];
        return S_OK;
    }
    CATCH_RETURN();
}
HRESULT ElementProvider::get_HorizontalScrollPercent(double* out) noexcept {
    return scroll_value(false, false, out);
}
HRESULT ElementProvider::get_VerticalScrollPercent(double* out) noexcept {
    return scroll_value(true, false, out);
}
HRESULT ElementProvider::get_HorizontalViewSize(double* out) noexcept {
    return scroll_value(false, true, out);
}
HRESULT ElementProvider::get_VerticalViewSize(double* out) noexcept {
    return scroll_value(true, true, out);
}
HRESULT ElementProvider::get_HorizontallyScrollable(BOOL* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    double p{};
    RETURN_IF_FAILED(get_HorizontalScrollPercent(&p));
    *out = p >= 0;
    return S_OK;
}
HRESULT ElementProvider::get_VerticallyScrollable(BOOL* out) noexcept {
    if (!out) {
        return E_POINTER;
    }
    double p{};
    RETURN_IF_FAILED(get_VerticalScrollPercent(&p));
    *out = p >= 0;
    return S_OK;
}
HRESULT ElementProvider::SetScrollPercent(double h, double v) noexcept {
    RETURN_IF_FAILED(actionable());
    if (element->layout_.kind != ElementLayout::Kind::scroll) {
        return UIA_E_NOTSUPPORTED;
    }
    const bool vertical = element->layout_.axis == layout::Axis::vertical;
    const auto p = vertical ? v : h;
    if ((vertical ? h : v) != UIA_ScrollPatternNoScroll) {
        return UIA_E_INVALIDOPERATION;
    }
    if (p == UIA_ScrollPatternNoScroll) {
        return S_OK;
    }
    if (!std::isfinite(p) || p < 0 || p > 100) {
        return E_INVALIDARG;
    }
    try {
        window.elements().update_layout();
        if (!element->scrollLimit_) {
            return UIA_E_INVALIDOPERATION;
        }
        element->set_scroll_offset(float(p * element->scrollLimit_ / 100));
        window.elements().update_layout();
        return S_OK;
    }
    CATCH_RETURN();
}
HRESULT ElementProvider::Scroll(ScrollAmount h, ScrollAmount v) noexcept {
    RETURN_IF_FAILED(actionable());
    if (element->layout_.kind != ElementLayout::Kind::scroll) {
        return UIA_E_NOTSUPPORTED;
    }
    const bool vertical = element->layout_.axis == layout::Axis::vertical;
    if ((vertical ? h : v) != ScrollAmount_NoAmount) {
        return UIA_E_INVALIDOPERATION;
    }
    float delta{};
    try {
        window.elements().update_layout();
        const auto page = vertical ? element->bounds_.height : element->bounds_.width;
        switch (vertical ? v : h) {
        case ScrollAmount_NoAmount:
            return S_OK;
        case ScrollAmount_LargeIncrement:
            delta = page;
            break;
        case ScrollAmount_LargeDecrement:
            delta = -page;
            break;
        case ScrollAmount_SmallIncrement:
            delta = 16;
            break;
        case ScrollAmount_SmallDecrement:
            delta = -16;
            break;
        default:
            return E_INVALIDARG;
        }
        if (!element->scrollLimit_) {
            return UIA_E_INVALIDOPERATION;
        }
        element->set_scroll_offset(std::clamp(element->scroll_offset() + delta, 0.f, element->scrollLimit_));
        window.elements().update_layout();
        return S_OK;
    }
    CATCH_RETURN();
}
}
