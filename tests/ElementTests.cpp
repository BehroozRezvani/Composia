#include <composia/Element.hpp>
#include "support/TestSupport.hpp"
#include <cmath>

using namespace composia;
using testing::require;
using Kind = ElementLayout::Kind;
using layout::Axis;

namespace {
class Probe : public Element {
public:
    using Element::Element;
    std::function<bool(const PointerEvent&)> pointer;
    std::function<bool(const KeyEvent&)> key;
    std::function<bool(const ContextMenuEvent&)> menu;
    mutable unsigned measures{};
    std::vector<bool> focusChanges;
    void on_focus_changed(bool value) override { focusChanges.push_back(value); }

    layout::Size measure_content(layout::Size available) const override {
        ++measures;
        return Element::measure_content(available);
    }
    bool on_pointer(const PointerEvent& e) override { return pointer && pointer(e); }
    bool on_key(const KeyEvent& e) override { return key && key(e); }
    bool on_context_menu(const ContextMenuEvent& e) override { return menu && menu(e); }
};

auto leaf(float w, float h) {
    auto child = std::make_shared<Probe>();
    child->set_preferred_size({w, h});
    return child;
}

bool same(layout::Rect a, layout::Rect b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

int arrangement(const testing::Options&) {
    auto root = std::make_shared<Element>(ElementLayout{Kind::stack, Axis::vertical, 4});
    auto flex = std::make_shared<Element>(ElementLayout{Kind::flex, Axis::horizontal, 4});
    auto grid = std::make_shared<Element>(ElementLayout{.kind = Kind::grid, .gap = 2, .columns = 2});
    auto a = leaf(20, 10), b = leaf(40, 8);
    a->set_flex_grow(1);
    b->set_flex_grow(3);
    flex->add(a);
    flex->add(b);
    for (float h : {3.f, 8.f, 12.f}) { grid->add(leaf(10, h)); }
    root->add(flex);
    root->add(grid);
    ElementTree tree(root);
    tree.set_viewport({104, 90});
    tree.update_layout();
    require(same(a->bounds(), {0, 0, 30, 10}) && same(b->bounds(), {34, 0, 70, 10}), "Flex weighted growth");
    require(same(grid->bounds(), {0, 14, 104, 22}), "Nested stack intrinsic height");
    require(same(grid->children()[0]->bounds(), {0, 0, 51, 8}) &&
        same(grid->children()[2]->bounds(), {0, 10, 51, 12}), "Grid row heights and column gaps");
    tree.set_viewport({34, 20});
    tree.update_layout();
    require(same(b->bounds(), {14, 0, 20, 10}), "Flex proportional shrink");
    require(grid->bounds().height == 6 && tree.hit_test({32, 20}) == nullptr, "Stack overflow or half-open viewport");
    auto clipped = grid->children()[2];
    require(clipped->viewport_bounds().y >= 20 && clipped->viewport_bounds(true).height == 0,
        "Screen geometry exposed a fully clipped row");
    clipped->set_focusable(true);
    require(!tree.focus(clipped.get()), "Fully clipped grid row took focus");
    flex->set_layout({Kind::stack, Axis::horizontal, 1});
    require(tree.hit_test({21, 0}) == b.get() && b->bounds().width == 13, "Replacing a layout left stale hit bounds");

    // Padding insets the children, and an element's intrinsic size includes it.
    auto padded = std::make_shared<Element>(ElementLayout{.kind = Kind::stack, .gap = 2, .padding = {10, 5, 20, 15}});
    auto first = leaf(30, 10), second = leaf(30, 6);
    padded->add(first);
    padded->add(second);
    auto bare = std::make_shared<Element>(ElementLayout{.padding = layout::Insets::all(7)});
    auto paddedColumn = std::make_shared<Element>(ElementLayout{Kind::stack});
    paddedColumn->add(padded);
    paddedColumn->add(bare);
    ElementTree paddedTree(paddedColumn);
    paddedTree.set_viewport({100, 100});
    paddedTree.update_layout();
    require(same(padded->bounds(), {0, 0, 100, 38}) && same(first->bounds(), {10, 5, 70, 10}) &&
        same(second->bounds(), {10, 17, 70, 6}), "Padding did not inset the children or add to the intrinsic height");
    require(same(bare->bounds(), {0, 38, 100, 14}), "An element without children was not as tall as its padding");
    require(paddedTree.hit_test({5, 8}) == padded.get() && paddedTree.hit_test({12, 8}) == first.get(),
        "Hit-testing in the padding reached a child");
    padded->remove(*second);
    padded->set_layout({.kind = Kind::scroll, .viewportLength = 20.f, .padding = {0, 4, 0, 4}});
    first->set_preferred_size({30, 40});
    paddedTree.update_layout();
    require(padded->bounds().height == 28 && first->bounds().y == 4 && padded->scroll_limit() == 20,
        "A scroll viewport did not scroll its content inside the padding");
    require(testing::throws<std::invalid_argument>([&] { padded->set_layout({.padding = {-1, 0, 0, 0}}); }), "Negative padding was accepted");

    class Wrapping : public Element {
        layout::Size measure_content(layout::Size available) const override {
            const float width = std::clamp(available.width, 1.f, 80.f);
            return {width, 10 * std::ceil(80 / width)};
        }
    };
    auto wrap = std::make_shared<Wrapping>();
    auto line = std::make_shared<Element>(ElementLayout{Kind::flex, Axis::horizontal});
    line->add(wrap);
    line->add(leaf(80, 8));
    auto column = std::make_shared<Element>(ElementLayout{Kind::stack});
    column->add(line);
    ElementTree wrapping(column);
    wrapping.set_viewport({80, 100});
    wrapping.update_layout();
    require(same(wrap->bounds(), {0, 0, 40, 20}), "Flex failed to remeasure at the allocated width");
    wrapping.set_viewport({160, 100});
    wrapping.update_layout();
    require(same(wrap->bounds(), {0, 0, 80, 10}), "Parent retained stale wrapped height after resize");

    class Fallible : public Element {
    public:
        mutable bool fail{};
        layout::Size measure_content(layout::Size available) const override {
            if (available.height == 30 && std::exchange(fail, false)) { throw std::runtime_error("measure"); }
            return {40, 60};
        }
    };
    auto item = std::make_shared<Fallible>();
    auto panel = std::make_shared<Element>();
    panel->add(item);
    auto stack = std::make_shared<Element>(ElementLayout{Kind::stack});
    stack->add(panel);
    ElementTree retry(stack);
    retry.set_viewport({80, 50});
    retry.update_layout();
    item->fail = true;
    retry.set_viewport({80, 30});
    require(testing::throws<std::runtime_error>([&] { retry.update_layout(); }), "Custom measurement failure was lost");
    retry.update_layout();
    require(item->bounds().height == 30, "Retry skipped an interrupted arrange at the same size");

    require(testing::throws<std::invalid_argument>([&] { grid->add(root); }), "Attached root accepted as a child");
    require(testing::throws<std::invalid_argument>([&] { root->add(b); }), "Child attached twice");
    require(testing::throws<std::invalid_argument>([&] { flex->set_layout({Kind::scroll}); }), "Scroll with multiple children");
    require(testing::throws<std::invalid_argument>([&] { b->set_flex_grow(-1); }), "Negative flex weight");
    require(testing::throws<std::invalid_argument>([&] { tree.set_viewport({INFINITY, 100}); }), "Infinite viewport");
    require(testing::throws<std::invalid_argument>([&] { grid->set_layout({.kind = Kind::grid, .columns = 0}); }), "Grid with no columns");
    auto detached = std::make_shared<Element>();
    auto branch = std::make_shared<Element>();
    detached->add(branch);
    require(testing::throws<std::invalid_argument>([&] { branch->add(detached); }), "Detached cycle accepted");
    detached.reset();
    require(branch->parent() == nullptr, "Child outliving its parent still points to it");
    return 0;
}

// The pointer's shape: an element's own, its parent's when it has none, a part's own through
// cursor_at, none from a disabled element, and the captured element's wherever the pointer is.
class Link final : public Element {
public:
    Link() { set_preferred_size({40, 20}); }
private:
    std::optional<Cursor> cursor_at(layout::Point local) const override {
        return local.x < 20 ? std::optional{Cursor::hand} : std::nullopt;
    }
};

class Grabber final : public Element {
public:
    Grabber() { set_preferred_size({40, 20}); }
    using Element::capture_pointer;
    using Element::release_pointer;
};

int cursors(const testing::Options&) {
    auto root = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack, layout::Axis::horizontal});
    auto text = leaf(40, 20), inner = leaf(10, 10);
    auto plain = std::make_shared<Grabber>();
    auto link = std::make_shared<Link>();
    root->add(text);
    root->add(plain);
    root->add(link);
    plain->add(inner);
    ElementTree tree(root);
    tree.set_viewport({120, 20});
    require(tree.cursor_at({5, 5}) == Cursor::arrow, "No shape set is not the arrow");
    text->set_cursor(Cursor::text);
    root->set_cursor(Cursor::help);
    require(tree.cursor_at({5, 5}) == Cursor::text && tree.cursor_at({45, 5}) == Cursor::help && tree.cursor_at({42, 2}) == Cursor::help,
        "An element's own shape or its parent's");
    require(tree.cursor_at({85, 5}) == Cursor::hand && tree.cursor_at({105, 5}) == Cursor::help, "A part's shape through cursor_at");
    text->set_enabled(false);
    require(tree.cursor_at({5, 5}) == Cursor::help, "A disabled element kept its shape");
    text->set_enabled(true);
    text->set_cursor(std::nullopt);
    plain->set_cursor(Cursor::resize_horizontal);
    require(plain->capture_pointer(), "Capture");
    require(tree.cursor_at({5, 5}) == Cursor::resize_horizontal, "The captured element's shape did not follow the pointer");
    plain->release_pointer();
    require(tree.cursor_at({5, 5}) == Cursor::help, "Releasing the capture kept its shape");
    return 0;
}

// Layout hooks: an element observing layout hears of it once bounds are final, may change the
// layout (another pass follows) or move the focus (it moves once layout settles), and a hook
// that never lets layout settle stops after a bounded number of passes. A canvas keeps its
// children at their offsets and measured sizes.
class Watcher final : public Element {
public:
    Watcher() { observe_layout(); set_preferred_size({30, 10}); }
    std::function<void()> hook;
    int calls{};
private:
    void on_layout_updated() override { ++calls; if (hook) { hook(); } }
};

int layout_hooks(const testing::Options&) {
    auto root = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::stack});
    auto watcher = std::make_shared<Watcher>();
    auto target = leaf(30, 10);
    target->set_focusable(true);
    root->add(watcher);
    root->add(target);
    ElementTree tree(root);
    tree.set_viewport({100, 100});
    tree.update_layout();
    require(watcher->calls >= 1, "A layout observer did not hear of layout");

    // A hook that changes the layout: another pass follows, with the new size.
    bool grown = false;
    watcher->hook = [&] { if (!grown) { grown = true; watcher->set_preferred_size({30, 40}); } };
    watcher->set_preferred_size({30, 20});
    tree.update_layout();
    require(grown && target->bounds().y == 40, "The layout a hook changed was not laid out again");

    // A hook that moves the focus: it moves once the layout settles.
    watcher->hook = [&] { (void)tree.focus(target.get()); };
    watcher->set_preferred_size({30, 12});
    tree.update_layout();
    require(target->focused() && tree.focused() == target.get(), "A focus move from a layout hook was lost");

    // A hook that never lets layout settle: update_layout still returns.
    float height = 10;
    watcher->hook = [&] { height = height == 10 ? 11 : 10; watcher->set_preferred_size({30, height}); };
    watcher->set_preferred_size({30, 13});
    tree.update_layout();
    watcher->hook = nullptr;
    tree.update_layout();

    // A canvas: children at their offsets, at their measured size.
    auto canvas = std::make_shared<Element>(ElementLayout{ElementLayout::Kind::canvas});
    canvas->set_preferred_size({100, 100});
    auto placed = leaf(20, 15);
    placed->set_offset({30, 40});
    canvas->add(placed);
    ElementTree canvasTree(canvas);
    canvasTree.set_viewport({100, 100});
    canvasTree.update_layout();
    const auto box = placed->viewport_bounds();
    require(box.x == 30 && box.y == 40 && box.width == 20 && box.height == 15, "A canvas child left its offset or measured size");
    return 0;
}

int routing(const testing::Options&) {
    auto root = std::make_shared<Probe>();
    auto back = leaf(10, 10), panel = leaf(40, 40), front = leaf(20, 20);
    panel->set_offset({12, 5});
    front->set_offset({3, 4});
    panel->add(front);
    root->add(back);
    root->add(panel);
    ElementTree tree(root);
    tree.set_viewport({80, 50});
    require(tree.hit_test({14, 9}) == panel.get() && tree.hit_test({15, 9}) == front.get(), "Nested hit offsets");
    back->set_z_index(1);
    require(tree.hit_test({15, 9}) == back.get(), "Picking ignored z order");
    back->set_z_index(0);
    std::vector<int> calls;
    front->set_focusable(true);
    back->set_focusable(true);
    front->pointer = [&](const auto& e) { calls.push_back(1); return e.position.x != 1 || e.position.y != 2; };
    panel->pointer = [&](const auto& e) { calls.push_back(2); return e.position.x == 4 && e.position.y == 6; };
    root->pointer = [&](const auto&) { calls.push_back(3); return false; };
    require(tree.pointer({PointerEvent::Kind::down, {16, 11}, PointerEvent::Button::left}) &&
        calls == std::vector<int>{1, 2} && front->focused(), "Pointer bubbling, local coordinates or focus");
    panel->key = [](const KeyEvent& e) { return e.key == 'A'; };
    require(tree.key({'A', true}), "Key did not bubble to panel");
    require(tree.key({VK_TAB, true}) && back->focused() && tree.key({VK_TAB, true, true}) && front->focused(),
        "Tab and Shift+Tab did not wrap in insertion order");
    require(tree.next_focus(false, false) == nullptr, "Last stop blocked exit to native controls");
    // A focusable element that keeps out of the focus a press gives: the press reaches it, and the
    // focus stays where it was.
    bool backPressed{};
    back->pointer = [&](const auto&) { backPressed = true; return true; };
    back->set_focus_on_press(false);
    require(tree.pointer({PointerEvent::Kind::down, {1, 1}, PointerEvent::Button::left}) && backPressed && front->focused() &&
        !back->focused(), "A press took the focus for an element that keeps out of it");
    back->set_focus_on_press(true);
    back->pointer = nullptr;

    std::weak_ptr<Probe> lifetime = front;
    front->pointer = [&](const auto&) {
        root->remove(*panel);
        require(front->focusChanges == std::vector<bool>{true, false, true, false},
            "Removing the focused subtree missed a focus transition");
        front.reset();
        panel.reset();
        require(!lifetime.expired() && tree.focused() == nullptr, "Removing a routed subtree lost lifetime or left focus");
        tree.pointer({PointerEvent::Kind::move, {1, 1}});
        return false;
    };
    calls.clear();
    tree.pointer({PointerEvent::Kind::move, {16, 11}});
    require(lifetime.expired() && calls == std::vector<int>{3, 3}, "Reentrant routing or detached recipient");
    tree.focus(back.get());
    root->set_enabled(false);
    require(!back->enabled() && !tree.focused() && !tree.move_focus() && !tree.focus(back.get()) &&
        !tree.pointer({PointerEvent::Kind::down, {1, 1}, PointerEvent::Button::left}), "Disabled subtree received input");
    root->set_enabled(true);
    require(tree.move_focus() && back->focused(), "Re-enabled tab stop was lost");
    struct Widget { Probe face; };
    auto widget = std::make_shared<Widget>();
    root->add(std::shared_ptr<Element>(widget, &widget->face));
    widget->face.pointer = [](const auto&) { return true; };
    require(tree.pointer({PointerEvent::Kind::move, {1, 1}}), "Route lost ownership of an embedded element");
    struct RemovesOnFocus : Probe {
        void on_focus_changed(bool gained) override {
            if (gained) { parent()->remove(*this); }
        }
    };
    auto transient = std::make_shared<RemovesOnFocus>();
    transient->set_preferred_size({20, 20});
    transient->set_focusable(true);
    transient->set_z_index(10);
    root->add(transient);
    std::weak_ptr<Element> removed = transient;
    transient.reset();
    tree.pointer({PointerEvent::Kind::down, {1, 1}, PointerEvent::Button::left});
    require(removed.expired() && !tree.focused(), "A control removed during pointer focus stayed in the route");
    return 0;
}

int damage(const testing::Options&) {
    auto root = std::make_shared<Element>(ElementLayout{Kind::scroll});
    auto list = std::make_shared<Element>(ElementLayout{Kind::stack});
    for (int i = 0; i < 2048; ++i) { list->add(leaf(60, 10)); }
    root->add(list);
    int updates{};
    ElementTree tree(root, [&] { ++updates; });
    tree.set_viewport({60, 40});
    require(same(tree.take_dirty_bounds().value(), {0, 0, 60, 40}) && updates == 1, "Initial damage scheduling");
    auto last = std::static_pointer_cast<Probe>(list->children().back());
    const unsigned measured = last->measures;
    last->invalidate();
    require(!tree.take_dirty_bounds() && updates == 1, "Offscreen invalidation scheduled a repaint");
    auto row = list->children()[1];
    row->invalidate({2, 0, 4, 4});
    row->invalidate({3, 1, 5, 3});
    require(same(tree.take_dirty_bounds().value(), {2, 10, 6, 4}) && updates == 2, "Local dirty bounds did not coalesce");
    row->set_offset({3, 5});
    require(same(tree.take_dirty_bounds().value(), {0, 10, 60, 15}), "Move omitted old or new clipped bounds");
    row->set_offset({});
    (void)tree.take_dirty_bounds();
    last->set_focusable(true);
    require(tree.move_focus() && tree.hit_test({0, 39}) == last.get() && root->scroll_offset() == 20440,
        "Navigating into a scroll viewport did not reveal the last item");
    require(!tree.pointer({PointerEvent::Kind::wheel, {1, 20}, {}, -1}), "Scroll at boundary swallowed wheel");
    require(tree.pointer({PointerEvent::Kind::wheel, {1, 20}, {}, 1}) && root->scroll_offset() == 20392,
        "Wheel did not scroll the viewport");
    require(tree.hit_test({1, 0}) == list->children()[2039].get(), "Scroll hit did not track fractional rows");
    require(last->measures == measured, "Painting state, hits or scrolling remeasured the document");
    const auto moved = list->children()[2040];
    list->remove(*moved);
    (void)tree.take_dirty_bounds();
    list->add(moved);
    tree.update_layout();
    require(moved->parent() == list.get() && moved->bounds().y == 20470, "Detached element could not be reused");
    return 0;
}

// Context menu requests start at the element under the pointer, or at the focused element for
// the keyboard, and bubble with positions local to each recipient.
int context_menu(const testing::Options&) {
    auto root = std::make_shared<Probe>(ElementLayout{Kind::stack});
    auto first = leaf(100, 40), second = leaf(100, 40);
    second->set_focusable(true);
    root->add(first);
    root->add(second);
    ElementTree tree(root);
    tree.set_viewport({200, 100});
    std::vector<std::pair<char, ContextMenuEvent>> seen;
    second->menu = [&](const ContextMenuEvent& e) { seen.emplace_back('s', e); return false; };
    root->menu = [&](const ContextMenuEvent& e) { seen.emplace_back('r', e); return true; };
    require(tree.context_menu(layout::Point{10, 50}) && seen.size() == 2 && seen[0].first == 's' &&
        seen[0].second.position.x == 10 && seen[0].second.position.y == 10 && !seen[0].second.keyboard &&
        seen[1].first == 'r' && seen[1].second.position.y == 50, "A click's request did not bubble with local positions");
    seen.clear();
    require(!tree.context_menu(std::nullopt) && seen.empty(), "A keyboard request without focus reached an element");
    tree.focus(second.get());
    require(tree.context_menu(std::nullopt) && seen.size() == 2 && seen[0].second.keyboard &&
        seen[0].second.position.x == 0 && seen[0].second.position.y == 40 && seen[1].second.position.y == 80,
        "A keyboard request did not start at the focused element's lower-left corner");
    seen.clear();
    second->set_enabled(false);
    require(!tree.context_menu(layout::Point{10, 50}) && seen.empty(), "A disabled element received a request");
    return 0;
}
}

int hover(const testing::Options&) {
    using Kind = PointerEvent::Kind;
    auto root = std::make_shared<Probe>(ElementLayout{ElementLayout::Kind::stack});
    auto first = leaf(100, 40), second = leaf(100, 40);
    root->add(first);
    root->add(second);
    ElementTree tree(root);
    tree.set_viewport({200, 100});
    tree.pointer({Kind::move, {10, 10}});
    require(first->hovered() && !second->hovered(), "The pointer did not hover the element under it");
    root->remove(*first);
    tree.update_layout();
    require(second->hovered() && !first->hovered(), "An element that moved under a still pointer was not hovered");
    auto replacement = leaf(100, 40);
    root->insert(0, replacement);
    tree.update_layout();
    require(replacement->hovered() && !second->hovered(), "An element added under a still pointer was not hovered");
    tree.pointer_leave();
    root->remove(*replacement);
    tree.update_layout();
    require(!second->hovered() && !root->hovered(), "Hover came back after the pointer left");

    // Scrolling moves the next element under a still pointer.
    auto scroll = std::make_shared<Probe>(ElementLayout{ElementLayout::Kind::scroll, Axis::vertical, 0, 1, 100.f});
    auto content = std::make_shared<Probe>(ElementLayout{ElementLayout::Kind::stack});
    std::vector<std::shared_ptr<Probe>> rows;
    for (int i = 0; i < 5; ++i) {
        rows.push_back(leaf(100, 40));
        content->add(rows.back());
    }
    scroll->add(content);
    ElementTree scrolled(scroll);
    scrolled.set_viewport({100, 100});
    scrolled.pointer({Kind::move, {10, 10}});
    require(rows[0]->hovered(), "The first row was not hovered");
    scroll->set_scroll_offset(40);
    scrolled.update_layout();
    require(rows[1]->hovered() && !rows[0]->hovered(), "Scrolling did not move the hover to the row under the pointer");
    return 0;
}

// Stack and flex children sit across the axis as their parent or they ask, and leftover room goes
// where the parent's main alignment puts it.
int alignment(const testing::Options&) {
    using layout::Align;
    auto column = std::make_shared<Element>(ElementLayout{Kind::stack, Axis::vertical, 0});
    auto stretched = leaf(30, 10), start = leaf(30, 10), centred = leaf(30, 10), ended = leaf(30, 10), wide = leaf(300, 10);
    start->set_align(Align::start);
    centred->set_align(Align::center);
    ended->set_align(Align::end);
    wide->set_align(Align::center);
    for (const auto& child : {stretched, start, centred, ended, wide}) { column->add(child); }
    ElementTree tree(column);
    tree.set_viewport({100, 100});
    tree.update_layout();
    require(same(stretched->bounds(), {0, 0, 100, 10}), "A child stretched across the column by default");
    require(same(start->bounds(), {0, 10, 30, 10}), "A start-aligned child kept its breadth at the start");
    require(same(centred->bounds(), {35, 20, 30, 10}), "A centred child sat in the middle");
    require(same(ended->bounds(), {70, 30, 30, 10}), "An end-aligned child sat at the end");
    require(same(wide->bounds(), {0, 40, 100, 10}), "A child broader than the column was not clamped to it");
    require(tree.hit_test({10, 25}) != centred.get() && tree.hit_test({40, 25}) == centred.get(), "Hit testing ignored the alignment");

    // The parent's alignment applies to every child without one of its own, and changing either
    // arranges them again.
    auto plain = leaf(30, 10);
    column->add(plain);
    column->set_layout(ElementLayout{.kind = Kind::stack, .crossAlign = Align::end});
    tree.update_layout();
    require(same(stretched->bounds(), {70, 0, 30, 10}) && same(plain->bounds(), {70, 50, 30, 10}), "The column's alignment was ignored");
    require(same(start->bounds(), {0, 10, 30, 10}), "A child's own alignment did not win over the column's");
    start->set_align(std::nullopt);
    tree.update_layout();
    require(same(start->bounds(), {70, 10, 30, 10}), "Clearing a child's alignment did not follow the column");
    start->set_align(Align::stretch);
    tree.update_layout();
    require(same(start->bounds(), {0, 10, 100, 10}), "A stretched child in an end-aligned column did not fill it");

    // A row centres children across its height, and leftover length goes where main alignment says.
    auto row = std::make_shared<Element>(ElementLayout{.kind = Kind::flex, .axis = Axis::horizontal, .gap = 10,
        .crossAlign = Align::center, .mainAlign = Align::center});
    auto a = leaf(20, 10), b = leaf(30, 40);
    row->add(a);
    row->add(b);
    ElementTree rowTree(row);
    rowTree.set_viewport({100, 40});
    rowTree.update_layout();
    require(same(a->bounds(), {20, 15, 20, 10}) && same(b->bounds(), {50, 0, 30, 40}), "A centred row misplaced its children");
    row->set_layout(ElementLayout{.kind = Kind::flex, .axis = Axis::horizontal, .gap = 10, .crossAlign = Align::end, .mainAlign = Align::end});
    rowTree.update_layout();
    require(same(a->bounds(), {40, 30, 20, 10}) && same(b->bounds(), {70, 0, 30, 40}), "An end-aligned row misplaced its children");
    // A flex weight takes the room, so main alignment has none to place.
    a->set_flex_grow(1);
    rowTree.update_layout();
    require(same(a->bounds(), {0, 30, 60, 10}) && same(b->bounds(), {70, 0, 30, 40}), "Main alignment moved a row whose weights took the room");
    // Children that overflow start at the start.
    a->set_flex_grow(0);
    rowTree.set_viewport({40, 40});
    rowTree.update_layout();
    require(a->bounds().x == 0, "Overflowing children did not start at the start");
    return 0;
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"layout", arrangement}, {"alignment", alignment}, {"routing", routing}, {"cursors", cursors}, {"layout-hooks", layout_hooks}, {"damage", damage},
        {"context-menu", context_menu}, {"hover", hover}});
}
