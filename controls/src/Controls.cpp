#include <composia/Controls.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace composia::controls {
Command::Command(std::function<void()> action) : execute_(std::move(action)) {
    if (!execute_) { throw std::invalid_argument("A command needs an action"); }
}
bool Command::execute() const {
    if (!enabled.get()) { return false; }
    auto action = execute_;
    action();
    return true;
}
State state_of(const TextBox& box) {
    auto s = box.selection();
    return {box.hovered(), box.pointer_pressed(), box.focused(), !box.interactive(), s.start != s.end};
}
Control::Control(std::wstring name, long role, ElementLayout layout) : Element(layout) {
    set_accessibility({.name = std::move(name), .controlType = role});
    set_focusable(true);
    set_preferred_size({96, 32});
}
State Control::state() const {
    const auto* a = accessibility();
    const bool selected = a && (a->toggle ? a->toggle->on : a->selected.value_or(false));
    return {hovered(), pressed_, focused(), !interactive(), selected};
}
std::wstring_view Control::name() const noexcept {
    return accessibility() ? std::wstring_view(accessibility()->name) : std::wstring_view{};
}
void Control::set_renderer(Renderer fn) { renderer_ = std::move(fn); invalidate(); }
void Control::set_tokens(StyleTokens tokens) { tokens_ = tokens; invalidate(); }
void Control::paint(ScopedSurfaceDraw& draw) {
    if (renderer_) { renderer_(draw, *this, state(), tokens_); }
}
void Control::set_pressed(bool value) {
    if (pressed_ != value) { pressed_ = value; invalidate(); }
}
void Control::on_focus_changed(bool gained) {
    if (!gained) { release_pointer(); set_pressed(false); }
}
void Control::on_capture_lost() noexcept {
    try { set_pressed(false); } catch (...) { LOG_CAUGHT_EXCEPTION(); }
}
bool Pressable::on_pointer(const PointerEvent& e) {
    if (e.button != PointerEvent::Button::left) { return false; }
    if (e.kind == PointerEvent::Kind::down) {
        pointerArmed_ = capture_pointer();
        set_pressed(pointerArmed_);
        return true;
    }
    if (e.kind == PointerEvent::Kind::up && pointerArmed_) {
        const bool fire = hovered() && interactive();
        release_pointer();
        set_pressed(false);
        if (fire) { activate(); }
        return true;
    }
    return false;
}
void Pressable::on_capture_lost() noexcept { pointerArmed_ = false; Control::on_capture_lost(); }
void Pressable::on_hover_changed(bool inside) {
    if (pointerArmed_) { set_pressed(inside); }
    Control::on_hover_changed(inside);
}
bool Pressable::on_key(const KeyEvent& e) {
    if (!focused() || e.alt || e.control || (e.key != VK_SPACE && e.key != VK_RETURN)) { return false; }
    if (pointerArmed_) { return true; }
    if (e.key == VK_RETURN) {
        if (e.down && !(e.keyData & (1L << 30))) { activate(); }
    } else if (e.down) {
        set_pressed(true);
    } else if (state().pressed) {
        set_pressed(false);
        activate();
    }
    return true;
}
Button::Button(std::wstring name, std::shared_ptr<Command> command)
    : Pressable(std::move(name), UIA_ButtonControlTypeId), command_(std::move(command)) {
    if (!command_) { throw std::invalid_argument("A button needs a command"); }
    set_available(command_->enabled.get());
    availability_ = command_->enabled.on_change([this](bool) { set_available(command_->enabled.get()); });
    auto a = *accessibility();
    a.invoke = [this] { activate(); };
    set_accessibility(std::move(a));
}
Button::Button(std::wstring name, std::function<void()> action)
    : Button(std::move(name), std::make_shared<Command>(std::move(action))) {}
void Button::activate() { auto command = command_; command->execute(); }

Toggle::Toggle(std::wstring name, bool checked) : Pressable(std::move(name), UIA_CheckBoxControlTypeId) {
    auto a = *accessibility();
    a.toggle = ElementAccessibility::Switch{checked, [this](bool on) { set_checked(on); }};
    set_accessibility(std::move(a));
}
bool Toggle::checked() const noexcept { return accessibility()->toggle->on; }
void Toggle::set_checked(bool on) {
    if (checked() == on) { return; }
    auto a = *accessibility();
    a.toggle->on = on;
    set_accessibility(std::move(a));
    changed_.emit(on);
}
void Toggle::activate() { set_checked(!checked()); }

Slider::Slider(std::wstring name, double minimum, double maximum, double value,
               double smallChange, double largeChange, layout::Axis axis)
    : Control(std::move(name), UIA_SliderControlTypeId), axis_(axis) {
    auto a = *accessibility();
    a.value = ElementAccessibility::RangeValue{value, minimum, maximum, smallChange, largeChange,
                                               [this](double v) { set_value(v); }};
    set_accessibility(std::move(a));
    if (axis == layout::Axis::vertical) { set_preferred_size({32, 96}); }
}
double Slider::value() const noexcept { return std::get<ElementAccessibility::RangeValue>(accessibility()->value).value; }
void Slider::set_value(double v) {
    if (v == value()) { return; }
    set_accessible_value(v);
    changed_.emit(v);
}
bool Slider::on_pointer(const PointerEvent& e) {
    if (e.kind == PointerEvent::Kind::down && e.button == PointerEvent::Button::left) { set_pressed(capture_pointer()); }
    if (!state().pressed || (e.kind != PointerEvent::Kind::move && e.button != PointerEvent::Button::left)) { return false; }
    const auto b = bounds();
    const bool vertical = axis_ == layout::Axis::vertical;
    const auto length = vertical ? b.height : b.width;
    if (e.kind == PointerEvent::Kind::up) { release_pointer(); }
    if (length > 0) {
        const auto fraction = std::clamp(double(vertical ? length - e.position.y : e.position.x) / length, 0., 1.);
        const auto& r = std::get<ElementAccessibility::RangeValue>(accessibility()->value);
        set_value(std::lerp(r.minimum, r.maximum, fraction));
    }
    return true;
}

ScrollView::ScrollView(std::wstring name, layout::Axis axis, float viewportLength)
    : Control(std::move(name), UIA_PaneControlTypeId, {ElementLayout::Kind::scroll, axis, 0, 1, viewportLength}), axis_(axis) {
    set_preferred_size(axis == layout::Axis::vertical ? layout::Size{96, viewportLength} : layout::Size{viewportLength, 32});
}
void ScrollView::set_content(std::shared_ptr<Element> content) {
    if (!children().empty()) {
        if (children().front() == content) { return; }
        if (content && (content.get() == this || content->parent())) { throw std::invalid_argument("Expected unattached content"); }
        auto old = children().front();
        remove(*old);
        try { if (content) { add(std::move(content)); } }
        catch (...) { add(std::move(old)); throw; }
    } else if (content) { add(std::move(content)); }
}
bool ScrollView::on_key(const KeyEvent& e) {
    if (!e.down || e.control || e.alt || e.shift) { return false; }
    const bool vertical = axis_ == layout::Axis::vertical;
    const auto page = vertical ? bounds().height : bounds().width;
    auto next = scroll_offset();
    if (e.key == VK_HOME) { next = 0; }
    else if (e.key == VK_END) { next = scroll_limit(); }
    else if (e.key == VK_PRIOR) { next -= page; }
    else if (e.key == VK_NEXT) { next += page; }
    else if (e.key == (vertical ? VK_UP : VK_LEFT)) { next -= 16; }
    else if (e.key == (vertical ? VK_DOWN : VK_RIGHT)) { next += 16; }
    else { return false; }
    next = std::clamp(next, 0.f, scroll_limit());
    if (next == scroll_offset()) { return false; }
    set_scroll_offset(next);
    return true;
}
}
