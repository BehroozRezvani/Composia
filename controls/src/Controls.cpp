#include <composia/Controls.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace composia::controls {
namespace {
bool light(std::uint32_t rgb) {
    const auto r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
    return 0.2126 * r + 0.7152 * g + 0.0722 * b > 0.5 * 255;
}
}

StyleTokens StyleTokens::from(const Appearance& appearance) {
    StyleTokens t;
    t.fontSize = 14 * appearance.textScale;
    if (appearance.highContrast) {
        const auto& c = appearance.colors;
        t.foreground = c.buttonText;
        t.background = t.hover = t.pressed = c.buttonFace;
        t.accent = c.highlight;
        t.accentForeground = c.highlightText;
        t.border = c.buttonText;
        t.focus = c.windowText;
        t.disabled = c.grayText;
        return t;
    }
    t.accent = appearance.accent;
    t.accentForeground = light(appearance.accent) ? 0x000000 : 0xffffff;
    if (appearance.dark) {
        t.foreground = t.focus = 0xffffff;
        t.background = 0x2b2b2b;
        t.hover = 0x363636;
        t.pressed = 0x262626;
        t.border = 0x5a5a5a;
        t.disabled = 0x787878;
    } else {
        t.foreground = 0x1b1b1b;
        t.focus = 0x000000;
        t.background = 0xfbfbfb;
        t.hover = 0xf0f0f0;
        t.pressed = 0xe6e6e6;
        t.border = 0x8a8a8a;
        t.disabled = 0xa0a0a0;
    }
    return t;
}

StyleScope::StyleScope(std::optional<StyleTokens> tokens, ElementLayout layout)
    : Element(layout), tokens_(std::move(tokens)) {}
void StyleScope::set_tokens(std::optional<StyleTokens> tokens) {
    if (tokens == tokens_) { return; }
    tokens_ = std::move(tokens);
    invalidate();
}
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
void Control::set_tokens(std::optional<StyleTokens> tokens) {
    if (tokens == tokens_) { return; }
    tokens_ = std::move(tokens);
    invalidate();
}
StyleTokens Control::tokens() const {
    if (tokens_) { return *tokens_; }
    for (auto* p = parent(); p; p = p->parent()) {
        if (auto* control = dynamic_cast<const Control*>(p); control && control->tokens_) { return *control->tokens_; }
        if (auto* scope = dynamic_cast<const StyleScope*>(p); scope && scope->tokens()) { return *scope->tokens(); }
    }
    return {};
}
bool Control::on_context_menu(const ContextMenuEvent& e) {
    if (!contextMenu_.connected()) { return false; }
    contextMenu_.emit(e);
    return true;
}
void Control::paint(ScopedSurfaceDraw& draw) {
    if (renderer_) { renderer_(draw, *this, state(), tokens()); }
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
namespace {
constexpr float barInset = 2, barThin = 2, barWide = 6, barReach = 12, thumbMinimum = 24;
}
std::optional<ScrollView::Thumb> ScrollView::thumb() const {
    const auto limit = scroll_limit();
    const auto viewport = axis_ == layout::Axis::vertical ? bounds().height : bounds().width;
    const float track = viewport - 2 * barInset;
    if (!scrollbar_ || limit <= 0 || track <= 0) { return std::nullopt; }
    const float length = std::min(track, std::max(thumbMinimum, track * viewport / (viewport + limit)));
    return Thumb{barInset + (track - length) * std::clamp(scroll_offset() / limit, 0.f, 1.f), length, track};
}
bool ScrollView::claims_pointer(layout::Point p) const {
    if (!thumb()) { return false; }
    return axis_ == layout::Axis::vertical ? p.x >= bounds().width - barReach : p.y >= bounds().height - barReach;
}
void ScrollView::set_scrollbar(bool visible) {
    if (scrollbar_ == visible) { return; }
    scrollbar_ = visible;
    if (!visible) { dragging_ = false; barHovered_ = false; release_pointer(); }
    invalidate();
}
void ScrollView::set_scrollbar_renderer(ScrollbarRenderer renderer) {
    scrollbarRenderer_ = std::move(renderer);
    invalidate();
}
void ScrollView::set_bar_hovered(bool value) {
    if (barHovered_ != value) { barHovered_ = value; invalidate(); }
}
bool ScrollView::on_pointer(const PointerEvent& e) {
    const bool vertical = axis_ == layout::Axis::vertical;
    const float along = vertical ? e.position.y : e.position.x;
    const auto t = thumb();
    if (dragging_ && t) {
        if (e.kind == PointerEvent::Kind::move) {
            const float room = t->track - t->length;
            const float fraction = room > 0 ? std::clamp((along - grab_ - barInset) / room, 0.f, 1.f) : 0.f;
            set_scroll_offset(fraction * scroll_limit());
            return true;
        }
        if (e.kind == PointerEvent::Kind::up && e.button == PointerEvent::Button::left) {
            dragging_ = false;
            release_pointer();
            set_bar_hovered(claims_pointer(e.position));
            return true;
        }
    }
    if (e.kind == PointerEvent::Kind::move) { set_bar_hovered(claims_pointer(e.position)); return false; }
    if (!t || e.kind != PointerEvent::Kind::down || e.button != PointerEvent::Button::left || !claims_pointer(e.position)) {
        return false;
    }
    if (along >= t->start && along < t->start + t->length) {
        grab_ = along - t->start;
        dragging_ = capture_pointer();
    } else {
        const auto page = vertical ? bounds().height : bounds().width;
        const auto next = scroll_offset() + (along < t->start ? -page : page);
        set_scroll_offset(std::clamp(next, 0.f, scroll_limit()));
    }
    return true;
}
void ScrollView::on_hover_changed(bool inside) {
    if (!inside && !dragging_) { barHovered_ = false; }
    Control::on_hover_changed(inside);
}
void ScrollView::on_capture_lost() noexcept {
    dragging_ = false;
    Control::on_capture_lost();
}
void ScrollView::paint_over(ScopedSurfaceDraw& draw) {
    const auto t = thumb();
    if (!t) { return; }
    const auto tokens = this->tokens();
    const bool wide = barHovered_ || dragging_;
    const float thickness = wide ? barWide : barThin;
    const bool vertical = axis_ == layout::Axis::vertical;
    const auto b = bounds();
    const float edge = (vertical ? b.width : b.height) - barInset - thickness;
    const auto rect = [&](float start, float length) {
        return vertical ? layout::Rect{edge, start, thickness, length} : layout::Rect{start, edge, length, thickness};
    };
    const Scrollbar bar{rect(barInset, t->track), rect(t->start, t->length), barHovered_, dragging_};
    if (scrollbarRenderer_) {
        scrollbarRenderer_(draw, *this, bar, tokens);
        return;
    }
    const auto fill = [&](layout::Rect r, float alpha) {
        draw.context()->FillRoundedRectangle({{r.x, r.y, r.x + r.width, r.y + r.height}, thickness / 2, thickness / 2},
            draw.solid_brush(tokens.foreground, alpha));
    };
    if (wide) { fill(bar.track, 0.08f); }
    fill(bar.thumb, wide ? 0.6f : 0.4f);
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
