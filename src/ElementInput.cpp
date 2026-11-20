#include <composia/Element.hpp>
#include <composia/Native.hpp>
#include "ElementAutomation.hpp"
#include <algorithm>

namespace composia {
template<class Handler>
bool ElementTree::bubble(Element& target, Handler&& handle) {
    // A nested dispatch gets independent storage. Ordinary pointer moves reuse the buffer.
    auto path = std::move(route_);
    const auto recycle = wil::scope_exit([&] { path.clear(); route_.swap(path); });
    for (auto* p = &target; p; p = p->parent_) {
        path.push_back({p->parent_ ? p->parent_->children_[p->siblingIndex_] : root_, {}});
    }
    layout::Point origin{};
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        const auto b = it->element->bounds();
        origin.x += b.x;
        origin.y += b.y;
        it->origin = origin;
    }
    for (const auto& [element, position] : path) {
        if (element->tree_ == this && handle(*element, position)) { return true; }
    }
    return false;
}

bool ElementTree::pointer(PointerEvent event) {
    auto* target = hit_test(event.position);
    if (!target || !target->enabled()) { return false; }
    if (event.kind == PointerEvent::Kind::down && event.button == PointerEvent::Button::left) {
        const auto keepTarget = reference(target);
        for (auto* p = target; p; p = p->parent_) {
            if (p->focusable_) { focus(p); break; }
        }
        if (target->tree_ != this || !target->enabled()) { return false; }
    }
    const auto viewportPosition = event.position;
    return bubble(*target, [&](Element& element, layout::Point origin) {
        event.position = {viewportPosition.x - origin.x, viewportPosition.y - origin.y};
        if (element.on_pointer(event)) { return true; }
        if (element.tree_ == this && event.kind == PointerEvent::Kind::wheel &&
            element.layout_.kind == ElementLayout::Kind::scroll && element.layout_.axis == event.wheelAxis) {
            const float offset = std::clamp(element.scrollOffset_ - event.wheelDelta * 48.0f, 0.0f, element.scrollLimit_);
            if (offset != element.scrollOffset_) {
                element.set_scroll_offset(offset);
                return true;
            }
        }
        return false;
    });
}

bool ElementTree::key(const KeyEvent& event) {
    update_layout();
    if (focused_ && bubble(*focused_, [&](Element& element, layout::Point) {
        return element.on_key(event) || (element.tree_ == this && element.accessibility_ && element.accessibility_key(event));
    })) {
        return true;
    }
    constexpr unsigned tab = 0x09;
    return event.down && event.key == tab && !event.alt && !event.control && move_focus(event.shift);
}

bool ElementTree::focus(Element* element) {
    root_->check_mutation();
    if (element) {
        if (element->tree_ != this || !element->focusable_ || !element->enabled() ||
            (window_ && !window_->enabled())) { return false; }
        for (auto* p = element; p; p = p->parent_) {
            if (p->detaching_) { return false; }
        }
        update_layout();
        if (!element->reveal(false)) { return false; }
        element->reveal(true);
    }
    if (focused_ == element) { return true; }
    const auto previous = reference(focused_);
    const auto next = reference(element);
    focused_ = element;
    if (previous) { previous->invalidate(); previous->on_focus_changed(false); }
    if (next && focused_ == element) { next->invalidate(); next->on_focus_changed(true); }
    if (window_ && window_->focused()) {
        if (previous && !previous->focused()) { ElementAutomation::focus_changed(*previous, false); }
        if (next && focused_ == element) { ElementAutomation::focus_changed(*next, true); }
    }
    return focused_ == element;
}

Element* Element::last_descendant() noexcept {
    auto* p = this;
    while (!p->children_.empty()) { p = p->children_.back().get(); }
    return p;
}

std::shared_ptr<Element> ElementTree::reference(Element* element) const noexcept {
    if (!element) { return {}; }
    return element->parent_ ? element->parent_->children_[element->siblingIndex_] : root_;
}

Element* Element::adjacent(bool backwards) noexcept {
    if (backwards) {
        if (!parent_ || siblingIndex_ == 0) { return parent_; }
        return parent_->children_[siblingIndex_ - 1]->last_descendant();
    }
    if (!children_.empty()) { return children_.front().get(); }
    for (auto* p = this; p->parent_; p = p->parent_) {
        const auto& siblings = p->parent_->children_;
        if (p->siblingIndex_ + 1 < siblings.size()) { return siblings[p->siblingIndex_ + 1].get(); }
    }
    return nullptr;
}

Element* ElementTree::next_focus(bool backwards, bool wrap) {
    update_layout();
    const auto first = [&] { return backwards ? root_->last_descendant() : root_.get(); };
    auto* candidate = focused_ ? focused_->adjacent(backwards) : first();
    for (;;) {
        if (!candidate) {
            if (!wrap || !focused_) { return nullptr; }
            candidate = first();
            wrap = false;
        }
        if (candidate->focusable_ && candidate->enabled() && candidate->reveal(false)) { return candidate; }
        candidate = candidate->adjacent(backwards);
    }
}

bool ElementTree::move_focus(bool backwards, bool wrap) {
    auto* next = next_focus(backwards, wrap);
    return next && focus(next);
}

}
