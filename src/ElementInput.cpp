#include <composia/Element.hpp>
#include <composia/HostedVisual.hpp>
#include <composia/HostedWindow.hpp>
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

Cursor ElementTree::cursor_at(layout::Point position) {
    for (auto* p = captured_ ? captured_ : hit_test(position); p; p = p->parent_) {
        if (!p->enabled()) { continue; }
        const auto box = p->viewport_bounds();
        if (const auto cursor = p->cursor_at({position.x - box.x, position.y - box.y})) { return *cursor; }
    }
    return Cursor::arrow;
}

bool ElementTree::press_keeps_focus(layout::Point position) {
    for (auto* p = captured_ ? captured_ : hit_test(position); p; p = p->parent_) {
        if (p->focusable_) { return !p->focusOnPress_; }
    }
    return false;
}

bool ElementTree::pointer(PointerEvent event) {
    pointerAt_ = event.position;
    auto* hit = hit_test(event.position);
    update_hover(hit);
    auto* target = captured_ && event.kind != PointerEvent::Kind::wheel ? captured_ : hit;
    if (!target || !target->enabled()) { return false; }
    if (event.kind == PointerEvent::Kind::down && event.button == PointerEvent::Button::left) {
        const auto keepTarget = reference(target);
        for (auto* p = target; p; p = p->parent_) {
            if (!p->focusable_) { continue; }
            if (p->focusOnPress_) { focus(p); }
            break;
        }
        if (target->tree_ != this || !target->enabled()) { return false; }
    }
    const auto viewportPosition = event.position;
    return bubble(*target, [&](Element& element, layout::Point origin) {
        event.position = {viewportPosition.x - origin.x, viewportPosition.y - origin.y};
        if (element.on_pointer(event)) { return true; }
        if (element.tree_ == this && event.kind == PointerEvent::Kind::wheel &&
            element.layout_.kind == ElementLayout::Kind::scroll && element.layout_.axis == event.wheelAxis) {
            // As far per detent as the system asks: lines of 16 DIPs, or the viewport's length.
            constexpr float line = 16;
            const auto& p = element.layout_.padding;
            const float page = event.wheelAxis == layout::Axis::vertical ? element.bounds_.height - p.top - p.bottom
                                                                         : element.bounds_.width - p.left - p.right;
            const float step = event.wheelPages ? std::max(line, page) : event.wheelLines * line;
            const float offset = std::clamp(element.scrollOffset_ - event.wheelDelta * step, 0.0f, element.scrollLimit_);
            if (offset != element.scrollOffset_) {
                element.set_scroll_offset(offset);
                return true;
            }
        }
        return false;
    });
}

bool Element::hovered() const noexcept { return hover_; }

void ElementTree::update_hover(Element* next) {
    if (hovered_ == next) { return; }
    auto* previous = std::exchange(hovered_, next);
    auto* common = next;
    while (common && !common->hover_) { common = common->parent_; }
    for (auto* p = previous; p != common; p = p->parent_) {
        p->hover_ = false;
        p->on_hover_changed(false);
    }
    for (auto* p = next; p != common; p = p->parent_) {
        p->hover_ = true;
        p->on_hover_changed(true);
    }
}
void ElementTree::pointer_leave() {
    pointerAt_.reset();
    update_hover(nullptr);
}

bool Element::request_focus() {
    return host_window() ? host_window()->focus_element(*this) : tree_ && tree_->focus(this);
}
bool Element::capture_pointer() {
    if (!tree_ || !enabled()) { return false; }
    if (tree_->captured_ == this) { return true; }
    tree_->release_capture();
    if (auto* w = host_window()) { w->capture_pointer(); }
    tree_->captured_ = this;
    return true;
}
void ElementTree::release_capture() noexcept {
    if (auto* previous = std::exchange(captured_, nullptr)) {
        if (window_) { window_->release_pointer(); }
        previous->on_capture_lost();
    }
}
void Element::release_pointer() noexcept {
    if (tree_ && tree_->captured_ == this) { tree_->release_capture(); }
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
bool ElementTree::context_menu(std::optional<layout::Point> position) {
    update_layout();
    Element* target{};
    layout::Point at{};
    if (position) {
        target = hit_test(*position);
        at = *position;
    } else if (focused_) {
        target = focused_;
        const auto visible = focused_->viewport_bounds(true);
        at = {visible.x, visible.y + visible.height};
    }
    if (!target || !target->enabled()) { return false; }
    return bubble(*target, [&](Element& element, layout::Point origin) {
        return element.on_context_menu({{at.x - origin.x, at.y - origin.y}, !position});
    });
}

bool ElementTree::text_input(std::wstring_view text) {
    return focused_ && focused_->enabled() && bubble(*focused_, [&](Element& e, layout::Point) {
        return e.on_text_input(text);
    });
}

bool ElementTree::focus(Element* element) {
    root_->check_mutation();
    // Moved from a layout hook, the focus would reveal bounds still settling: it moves after.
    if (updatingLayout_ && element) {
        pendingFocus_ = reference(element);
        return true;
    }
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

Element* ElementTree::next_focus(bool backwards, bool wrap) { return next_focus_from(focused_, backwards, wrap); }

Element* ElementTree::next_focus_from(Element* from, bool backwards, bool wrap) {
    update_layout();
    const auto first = [&] { return backwards ? root_->last_descendant() : root_.get(); };
    auto* candidate = from ? from->adjacent(backwards) : first();
    for (;;) {
        if (!candidate) {
            if (!wrap || !from) { return nullptr; }
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

void ElementTree::place_windows() {
    if (!window_ || (hosted_.empty() && visuals_.empty())) { return; }
    update_layout();
    for (auto* visual : visuals_) { visual->place(*window_); }
    // Moving a window sends messages; a handler can detach other hosted elements meanwhile.
    const auto snapshot = hosted_;
    for (auto* hosted : snapshot) {
        if (std::ranges::find(hosted_, hosted) != hosted_.end()) { hosted->place(*window_); }
    }
}

}
