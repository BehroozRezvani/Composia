#include <composia/Element.hpp>
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
    tree_ = tree;
    for (auto& child : children_) { child->attach(tree); }
}

void Element::add(std::shared_ptr<Element> child) {
    check_mutation();
    if (!child || child->parent_ || child->tree_) { throw std::invalid_argument("Expected an unattached child"); }
    for (auto* p = this; p; p = p->parent_) {
        if (p == child.get()) { throw std::invalid_argument("An element cannot contain an ancestor"); }
    }
    if (layout_.kind == ElementLayout::Kind::scroll && !children_.empty()) {
        throw std::invalid_argument("A scroll element accepts one child");
    }
    children_.push_back(child);
    child->siblingIndex_ = children_.size() - 1;
    child->parent_ = this;
    child->attach(tree_);
    orderDirty_ = true;
    // A reattached subtree can already have the exact box it will receive in this parent.
    child->invalidate();
    mark_layout(true);
}

void Element::remove(Element& child) {
    check_mutation();
    if (child.parent_ != this) { throw std::invalid_argument("Element is not a direct child"); }
    const auto index = child.siblingIndex_;
    child.invalidate();
    if (tree_) {
        for (auto* p = tree_->focused_; p; p = p->parent_) {
            if (p == &child) { tree_->focused_ = nullptr; break; }
        }
    }
    child.attach(nullptr);
    child.parent_ = nullptr;
    children_.erase(children_.begin() + index);
    for (auto i = index; i < children_.size(); ++i) { children_[i]->siblingIndex_ = i; }
    orderDirty_ = true;
    mark_layout(true);
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
    focusable_ = value;
    if (!value && focused()) { tree_->focus(nullptr); }
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
    for (auto* child : visible_children({local.x, local.y, 0, 0}, true) | std::views::reverse) {
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
}

ElementTree::ElementTree(std::shared_ptr<Element> root, std::function<void()> requestUpdate)
    : root_(std::move(root)), requestUpdate_(std::move(requestUpdate)) {
    if (!root_ || root_->parent_ || root_->tree_) { throw std::invalid_argument("Expected an unattached root element"); }
    root_->mark_layout(true);
    root_->attach(this);
}

ElementTree::~ElementTree() { root_->attach(nullptr); }

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
    if (!root_->arrangeDirty_) { return; }
    traversing_ = true;
    const auto reset = wil::scope_exit([&] { traversing_ = false; });
    root_->measure(viewport_);
    root_->arrange({0, 0, viewport_.width, viewport_.height});
    if (focused_ && !focused_->reveal(false)) { focused_ = nullptr; }
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
