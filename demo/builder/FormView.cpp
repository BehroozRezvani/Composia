#include "FormView.hpp"
#include "../DemoButton.hpp"
#include <algorithm>
#include <cmath>

namespace builder {
using composia::layout::Rect;

FormView::FormView(composia::Window& host, Document document) : host_(host), document_(std::move(document)) {
    // Controls are created in draw order so later siblings stack above earlier ones.
    for (const auto& placed : document_.resolve()) {
        const auto& widget = *document_.find(placed.id);
        if (widget.kind == Kind::button) {
            auto button = std::make_unique<composia::Button>(host_, widget.text, demo::paint_button);
            clicks_.push_back(button->on_click([this, id = widget.id] {
                if (const auto widget = document_.find(id)) { clicked_.emit(*widget); }
            }));
            buttons_.emplace_back(widget.id, std::move(button));
        } else if (widget.kind == Kind::text_field) {
            fields_.emplace_back(widget.id, std::make_unique<TextField>(host_, widget.text));
        }
    }
}

FormView::~FormView() = default;

void FormView::arrange(Rect bounds) {
    bounds_ = bounds;
    placements_ = document_.resolve(bounds.width, bounds.height);
    const auto place = [&](unsigned id, composia::Window& control) {
        const auto it = std::ranges::find(placements_, id, &Placement::id);
        if (it != placements_.end()) { control.set_bounds(offset(it->bounds, {bounds_.x, bounds_.y})); }
    };
    for (auto& [id, button] : buttons_) { place(id, *button); }
    for (auto& [id, field] : fields_) { place(id, *field); }
}

void FormView::draw(FormPainter& p) {
    regions_.clear();
    const composia::numerics::float2 origin{bounds_.x, bounds_.y};
    for (const auto& placed : placements_) {
        const auto widget = document_.find(placed.id);
        if (!widget) { continue; }
        const auto clip = ancestor_clip(document_, placements_, *widget, origin, bounds_);
        const auto r = offset(placed.bounds, origin);
        const auto visible = intersect(clip, r);
        if (visible.width <= 0 || visible.height <= 0) { continue; }
        p.clip(clip);
        paint_widget(p, *widget, r);
        p.unclip();
        if (widget->kind == Kind::checkbox) { regions_.push_back({Interactive::checkbox, widget->id, visible}); }
        else if (widget->kind == Kind::slider) { regions_.push_back({Interactive::slider, widget->id, visible}); }
    }
}

bool FormView::interactive_at(composia::numerics::float2 point) const noexcept {
    return std::ranges::any_of(regions_, [&](const Region& region) { return region.bounds.contains(point.x, point.y); });
}

FormView::Pointer FormView::pointer_down(composia::numerics::float2 point) {
    for (auto it = regions_.rbegin(); it != regions_.rend(); ++it) {
        if (!it->bounds.contains(point.x, point.y)) { continue; }
        const auto widget = document_.find(it->id);
        if (!widget) { continue; }
        if (it->kind == Interactive::checkbox) {
            widget->checked = !widget->checked;
            changed_.emit(*widget);
            return Pointer::handled;
        }
        sliderDrag_ = it->id;
        set_slider(it->id, point.x);
        return Pointer::dragging;
    }
    return Pointer::ignored;
}

void FormView::pointer_move(composia::numerics::float2 point) {
    if (sliderDrag_) { set_slider(*sliderDrag_, point.x); }
}

void FormView::set_slider(unsigned id, float x) {
    const auto widget = document_.find(id);
    const auto placed = widget_bounds(id);
    if (!widget || !placed) { return; }
    const float ratio = std::clamp((x - (placed->x + 8)) / std::max(1.0f, placed->width - 16), 0.0f, 1.0f);
    const int value = static_cast<int>(std::lround(ratio * 100));
    if (widget->value == value) { return; }
    widget->value = value;
    changed_.emit(*widget);
}

std::optional<Rect> FormView::widget_bounds(unsigned id) const noexcept {
    const auto it = std::ranges::find(placements_, id, &Placement::id);
    if (it == placements_.end()) { return std::nullopt; }
    return offset(it->bounds, {bounds_.x, bounds_.y});
}

composia::Button* FormView::button(unsigned id) noexcept {
    for (auto& [widget, button] : buttons_) { if (widget == id) { return button.get(); } }
    return nullptr;
}

TextField* FormView::field(unsigned id) noexcept {
    for (auto& [widget, field] : fields_) { if (widget == id) { return field.get(); } }
    return nullptr;
}

}
