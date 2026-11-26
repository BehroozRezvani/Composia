#include <composia/Element.hpp>
#include <composia/HostedVisual.hpp>
#include <composia/HostedWindow.hpp>
#include "ElementAutomation.hpp"
#include <composia/ScopedSurfaceDraw.hpp>
#include <algorithm>
#include <cmath>
#include <ranges>
#include <stdexcept>

namespace composia {
namespace {
using layout::Point;
using layout::Rect;
using layout::Size;

bool empty(Rect r) { return r.width <= 0 || r.height <= 0; }
Rect intersection(Rect a, Rect b) {
    const float x = std::max(a.x, b.x), y = std::max(a.y, b.y);
    return {x, y, std::min(a.x + a.width, b.x + b.width) - x,
                  std::min(a.y + a.height, b.y + b.height) - y};
}
void validate_size(Size s) {
    if (!std::isfinite(s.width) || !std::isfinite(s.height) || s.width < 0 || s.height < 0) {
        throw std::invalid_argument("Element dimensions must be finite and nonnegative");
    }
}
}

Element::~Element() {
    for (auto& child : children_) { child->parent_ = nullptr; }
}

void Element::check_mutation() const {
    if (tree_ && tree_->traversing_) { throw std::logic_error("Cannot edit the tree during layout or painting"); }
}

void Element::attach(ElementTree* tree) noexcept {
    if (tree_ && !tree) {
        if (tree_->captured_ == this) { tree_->release_capture(); }
        if (tree_->hovered_ == this) { tree_->hovered_ = nullptr; }
        hover_ = false;
        ElementAutomation::detach(*this);
        on_detaching();
    }
    if (hostsWindow_ && tree_ != tree) {
        auto* self = static_cast<HostedWindow*>(this);
        if (tree_) { std::erase(tree_->hosted_, self); }
        if (tree) {
            try { tree->hosted_.push_back(self); }
            catch (...) { LOG_CAUGHT_EXCEPTION(); }
        }
    }
    if (hostsVisual_ && tree_ != tree) {
        auto* self = static_cast<HostedVisual*>(this);
        if (tree_) { std::erase(tree_->visuals_, self); }
        if (tree) {
            try { tree->visuals_.push_back(self); }
            catch (...) { LOG_CAUGHT_EXCEPTION(); }
        }
    }
    tree_ = tree;
    for (auto& child : children_) { child->attach(tree); }
}

void Element::add(std::shared_ptr<Element> child) {
    insert(children_.size(), std::move(child));
}

void Element::insert(std::size_t index, std::shared_ptr<Element> child) {
    check_mutation();
    if (index > children_.size()) { throw std::out_of_range("Invalid child index"); }
    if (!child || child->parent_ || child->tree_) { throw std::invalid_argument("Expected an unattached child"); }
    for (auto* p = this; p; p = p->parent_) {
        if (p == child.get()) { throw std::invalid_argument("An element cannot contain an ancestor"); }
    }
    if (layout_.kind == ElementLayout::Kind::scroll && !children_.empty()) {
        throw std::invalid_argument("A scroll element accepts one child");
    }
    children_.insert(children_.begin() + index, child);
    for (auto i = index; i < children_.size(); ++i) { children_[i]->siblingIndex_ = i; }
    child->parent_ = this;
    child->attach(tree_);
    orderDirty_ = true;
    // A reattached subtree can already have the exact box it will receive in this parent.
    child->invalidate();
    mark_layout(true);
    ElementAutomation::reconcile_selection(*this, child.get());
    ElementAutomation::structure(*this);
}

void Element::remove(Element& child) {
    check_mutation();
    if (child.parent_ != this) { throw std::invalid_argument("Element is not a direct child"); }
    if (child.detaching_) { return; }
    const auto keepChild = children_[child.siblingIndex_];
    child.detaching_ = true;
    const auto reset = wil::scope_exit([&] { child.detaching_ = false; });
    child.invalidate();
    if (tree_) {
        for (auto* p = tree_->focused_; p; p = p->parent_) {
            if (p == &child) { tree_->focus(nullptr); break; }
        }
    }
    const auto index = child.siblingIndex_;
    if (tree_ && child.hover_) { tree_->update_hover(this); }
    child.attach(nullptr);
    child.parent_ = nullptr;
    children_.erase(children_.begin() + index);
    for (auto i = index; i < children_.size(); ++i) { children_[i]->siblingIndex_ = i; }
    orderDirty_ = true;
    mark_layout(true);
    ElementAutomation::reconcile_selection(*this);
    ElementAutomation::structure(*this);
}

void Element::set_preferred_size(Size value) {
    check_mutation();
    validate_size(value);
    if (preferred_.width == value.width && preferred_.height == value.height) { return; }
    preferred_ = value;
    invalidate_measure();
}

void Element::set_flex_grow(float value) {
    check_mutation();
    validate_size({value, 0});
    if (grow_ == value) { return; }
    grow_ = value;
    if (parent_) { parent_->mark_layout(true); }
}

void Element::set_offset(Point value) {
    check_mutation();
    if (!std::isfinite(value.x) || !std::isfinite(value.y)) { throw std::invalid_argument("Expected a finite offset"); }
    if (offset_.x == value.x && offset_.y == value.y) { return; }
    offset_ = value;
    if (parent_) { parent_->orderDirty_ = true; }
    mark_layout(false);
}

void Element::set_z_index(int value) {
    check_mutation();
    if (z_ == value) { return; }
    z_ = value;
    if (parent_) { parent_->orderDirty_ = true; invalidate(); }
}

void Element::set_focusable(bool value) {
    check_mutation();
    if (focusable_ == value) { return; }
    focusable_ = value;
    ElementAutomation::geometry_changed(*this);
    if (tree_) { tree_->schedule(); }
    if (!value && focused()) { tree_->focus(nullptr); }
}

bool Element::enabled() const noexcept {
    for (auto* p = this; p; p = p->parent_) {
        if (!p->enabled_ || !p->available_) { return false; }
    }
    return true;
}

void Element::set_enabled(bool value) {
    check_mutation();
    if (enabled_ == value) { return; }
    enabled_ = value;
    enabled_changed();
}
void Element::set_available(bool value) {
    check_mutation();
    if (available_ == value) { return; }
    available_ = value;
    enabled_changed();
}
void Element::enabled_changed() {
    if (tree_ && tree_->captured_ && !tree_->captured_->enabled()) { tree_->release_capture(); }
    ElementAutomation::geometry_changed(*this);
    if (tree_) { tree_->schedule(); }
    if (tree_ && tree_->focused_ && !tree_->focused_->enabled()) { tree_->focus(nullptr); }
    invalidate();
}
bool Element::interactive() const noexcept {
    auto* w = host_window();
    return enabled() && (!w || w->enabled());
}

void Element::set_scroll_offset(float value) {
    check_mutation();
    if (layout_.kind != ElementLayout::Kind::scroll || !std::isfinite(value)) {
        throw std::invalid_argument("Expected a scroll container and a finite offset");
    }
    value = std::max(0.0f, value);
    if (scrollOffset_ == value) { return; }
    scrollOffset_ = value;
    mark_layout(false);
}

bool Element::focused() const noexcept { return tree_ && tree_->focused_ == this; }

void Element::set_tooltip(std::wstring value) {
    if (tooltip_ == value) { return; }
    tooltip_ = std::move(value);
    // The hosting window shows the new text, or hides a tooltip that became empty.
    if (tree_) { tree_->schedule(); }
}

ElementWindow* Element::host_window() const noexcept { return tree_ ? tree_->window_ : nullptr; }
void Element::update_host_layout() { if (tree_) { tree_->update_layout(); } }

Rect Element::viewport_bounds(bool clipped) const noexcept {
    auto box = bounds_;
    for (auto* p = parent_; p; p = p->parent_) {
        if (clipped) { box = intersection(box, {0, 0, p->bounds_.width, p->bounds_.height}); }
        box.x += p->bounds_.x;
        box.y += p->bounds_.y;
    }
    if (clipped && tree_) { box = intersection(box, {0, 0, tree_->viewport_.width, tree_->viewport_.height}); }
    box.width = std::max(0.f, box.width);
    box.height = std::max(0.f, box.height);
    return box;
}

void Element::invalidate() { invalidate({0, 0, bounds_.width, bounds_.height}); }

void Element::invalidate(Rect r) {
    if (!tree_) { return; }
    validate_size({r.width, r.height});
    if (!std::isfinite(r.x) || !std::isfinite(r.y)) { throw std::invalid_argument("Expected a finite rectangle"); }
    for (auto* p = this; p; p = p->parent_) {
        const auto box = p->bounds_;
        r = intersection(r, {0, 0, box.width, box.height});
        if (empty(r)) { return; }
        r.x += box.x;
        r.y += box.y;
    }
    tree_->damage(r);
}

void Element::mark_layout(bool measure) {
    ElementAutomation::geometry_changed(*this);
    for (auto* p = this; p; p = p->parent_) {
        if (p->arrangeDirty_ && (!measure || p->measureDirty_)) { break; }
        p->arrangeDirty_ = true;
        if (measure) { p->measureDirty_ = true; }
    }
    if (tree_) { tree_->schedule(); }
}

void Element::invalidate_measure() {
    check_mutation();
    invalidate();
    mark_layout(true);
}

std::span<Element* const> Element::visible_children(Rect local, bool point) {
    const bool vertical = layout_.kind == ElementLayout::Kind::grid || layout_.axis == layout::Axis::vertical;
    if (orderDirty_) {
        using enum ElementLayout::Kind;
        orderedBands_ = layout_.kind == stack || layout_.kind == flex || layout_.kind == grid;
        order_.clear();
        order_.reserve(children_.size());
        for (auto& child : children_) {
            order_.push_back(child.get());
            if (child->z_ || (vertical ? child->offset_.y : child->offset_.x) != 0) { orderedBands_ = false; }
        }
        if (!std::ranges::is_sorted(order_, {}, &Element::z_)) {
            std::stable_sort(order_.begin(), order_.end(), [](auto a, auto b) { return a->z_ < b->z_; });
        }
        orderDirty_ = false;
    }
    std::span<Element* const> all = order_;
    if (!orderedBands_ || all.size() < 8) { return all; }
    // Ordinary rows and columns have monotone edges. Skip offscreen siblings without walking
    // the document; translated and z-ordered siblings retain the general traversal.
    const float start = vertical ? local.y : local.x;
    const float end = start + (vertical ? local.height : local.width);
    const auto first = std::ranges::partition_point(all, [&](auto* e) {
        const auto b = e->bounds_;
        return (vertical ? b.y + b.height : b.x + b.width) <= start;
    });
    const auto last = std::partition_point(first, all.end(), [&](auto* e) {
        const float edge = vertical ? e->bounds_.y : e->bounds_.x;
        return point ? edge <= start : edge < end;
    });
    return {first, last};
}

Element* Element::hit(Point local) {
    if (claims_pointer(local)) { return this; }
    for (auto* child : visible_children({local.x, local.y, 0, 0}, true) | std::views::reverse) {
        if (child->hostsVisual_) { continue; }   // Hosted visuals let the pointer through.
        const auto box = child->bounds_;
        if (box.contains(local.x, local.y)) { return child->hit({local.x - box.x, local.y - box.y}); }
    }
    return this;
}

void Element::draw(ScopedSurfaceDraw& surface, Point origin, Rect clip) {
    origin.x += bounds_.x;
    origin.y += bounds_.y;
    clip = intersection(clip, {origin.x, origin.y, bounds_.width, bounds_.height});
    if (empty(clip)) { return; }
    const auto dc = surface.context().get();
    D2D1::Matrix3x2F base;
    dc->GetTransform(&base);
    dc->PushAxisAlignedClip({clip.x, clip.y, clip.x + clip.width, clip.y + clip.height}, D2D1_ANTIALIAS_MODE_ALIASED);
    const auto restore = wil::scope_exit([&] { dc->SetTransform(base); dc->PopAxisAlignedClip(); });
    dc->SetTransform(D2D1::Matrix3x2F::Translation(origin.x, origin.y) * base);
    paint(surface);
    dc->SetTransform(base);
    for (auto* child : visible_children({clip.x - origin.x, clip.y - origin.y, clip.width, clip.height})) {
        child->draw(surface, origin, clip);
    }
    dc->SetTransform(D2D1::Matrix3x2F::Translation(origin.x, origin.y) * base);
    paint_over(surface);
}

ElementTree::ElementTree(std::shared_ptr<Element> root, std::function<void()> requestUpdate)
    : root_(std::move(root)), requestUpdate_(std::move(requestUpdate)) {
    if (!root_ || root_->parent_ || root_->tree_) { throw std::invalid_argument("Expected an unattached root element"); }
    root_->mark_layout(true);
    root_->attach(this);
}

ElementTree::~ElementTree() { root_->attach(nullptr); }

void ElementTree::release_window() noexcept {
    if (!window_) { return; }
    focused_ = nullptr;
    root_->attach(nullptr);
    window_ = nullptr;
    root_->attach(this);
}

void ElementTree::schedule() {
    if (scheduled_) { return; }
    scheduled_ = true;
    try { if (requestUpdate_) { requestUpdate_(); } }
    catch (...) { scheduled_ = false; throw; }
}

void ElementTree::damage(Rect r) {
    r = intersection(r, {0, 0, viewport_.width, viewport_.height});
    if (empty(r)) { return; }
    if (dirty_) {
        const auto a = *dirty_;
        const float x = std::min(a.x, r.x), y = std::min(a.y, r.y);
        dirty_ = Rect{x, y, std::max(a.x + a.width, r.x + r.width) - x, std::max(a.y + a.height, r.y + r.height) - y};
    } else { dirty_ = r; }
    schedule();
}

void ElementTree::set_viewport(Size value) {
    root_->check_mutation();
    validate_size(value);
    if (viewport_.width == value.width && viewport_.height == value.height) { return; }
    viewport_ = value;
    dirty_.reset();
    damage({0, 0, value.width, value.height});
    root_->mark_layout(true);
}

void ElementTree::update_layout() {
    root_->check_mutation();
    if (updatingLayout_ || (!root_->arrangeDirty_ && layoutCallbacks_.empty())) { return; }
    updatingLayout_ = true;
    const auto reset = wil::scope_exit([&] { updatingLayout_ = traversing_ = false; });
    do {
        traversing_ = true;
        root_->measure(viewport_);
        root_->arrange({0, 0, viewport_.width, viewport_.height});
        traversing_ = false;
        while (!layoutCallbacks_.empty()) {
            const auto e = layoutCallbacks_.back();
            if (e->tree_ == this && e->observeLayout_) { e->on_layout_updated(); }
            layoutCallbacks_.pop_back();
        }
    } while (root_->arrangeDirty_);
    if (focused_ && !focused_->reveal(false)) {
        auto old = reference(std::exchange(focused_, nullptr));
        old->on_focus_changed(false);
        if (window_ && window_->focused()) { ElementAutomation::focus_changed(*old, false); }
    }
}

std::optional<Rect> ElementTree::take_dirty_bounds() {
    update_layout();
    scheduled_ = false;
    return std::exchange(dirty_, std::nullopt);
}

void ElementTree::paint(ScopedSurfaceDraw& surface) {
    update_layout();
    traversing_ = true;
    const auto reset = wil::scope_exit([&] { traversing_ = false; });
    const auto r = surface.update_bounds();
    surface.context()->Clear(D2D1::ColorF(0, 0.0f));
    root_->draw(surface, {}, {r.left, r.top, r.right - r.left, r.bottom - r.top});
}

void ElementTree::paint(ScopedSurfaceDraw& surface, Rect area) {
    update_layout();
    traversing_ = true;
    const auto reset = wil::scope_exit([&] { traversing_ = false; });
    surface.context()->Clear(D2D1::ColorF(0, 0.0f));
    root_->draw(surface, {-area.x, -area.y}, {0, 0, area.width, area.height});
}

Element* ElementTree::hit_test(Point p) {
    update_layout();
    const auto r = root_->bounds_;
    if (p.x < 0 || p.y < 0 || p.x >= viewport_.width || p.y >= viewport_.height || !r.contains(p.x, p.y)) { return nullptr; }
    return root_->hit({p.x - r.x, p.y - r.y});
}

bool Element::reveal(bool scroll) {
    Rect box{0, 0, bounds_.width, bounds_.height};
    for (auto* p = this; p; p = p->parent_) {
        if (p != this && p->layout_.kind == ElementLayout::Kind::scroll) {
            const bool vertical = p->layout_.axis == layout::Axis::vertical;
            auto& start = vertical ? box.y : box.x;
            const float length = vertical ? box.height : box.width;
            const float viewport = vertical ? p->bounds_.height : p->bounds_.width;
            const float delta = start < 0 ? start : std::max(0.0f, start + std::min(viewport, length) - viewport);
            const float offset = std::clamp(p->scrollOffset_ + delta, 0.0f, p->scrollLimit_);
            start -= offset - p->scrollOffset_;
            if (scroll) { p->set_scroll_offset(offset); }
        }
        box = intersection(box, {0, 0, p->bounds_.width, p->bounds_.height});
        if (empty(box)) { return false; }
        box.x += p->bounds_.x;
        box.y += p->bounds_.y;
    }
    return !empty(intersection(box, {0, 0, tree_->viewport_.width, tree_->viewport_.height}));
}

}
