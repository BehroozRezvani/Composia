#include <composia/Controls.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace composia::controls {
namespace {
class PopupLayer final : public Element {
public:
    explicit PopupLayer(std::weak_ptr<Picker> picker)
        : Element({ElementLayout::Kind::canvas}), picker_(std::move(picker)) {
        set_z_index(std::numeric_limits<int>::max());
    }
private:
    bool on_pointer(const PointerEvent& e) override {
        if (e.kind == PointerEvent::Kind::down &&
            (children().empty() || !children().front()->bounds().contains(e.position.x, e.position.y))) {
            if (auto picker = picker_.lock()) { picker->close(); }
        }
        return true;
    }
    std::weak_ptr<Picker> picker_;
};
}

PopupHost::PopupHost(std::shared_ptr<Element> content) {
    add(std::move(content));
    observe_layout();
}
PopupHost::~PopupHost() {
    try { dismiss(); } catch (...) { LOG_CAUGHT_EXCEPTION(); }
}
void PopupHost::present(Picker& picker) {
    dismiss();
    auto siblings = picker.parent()->children();
    auto it = std::find_if(siblings.begin(), siblings.end(), [&](const auto& p) { return p.get() == &picker; });
    auto anchor = std::static_pointer_cast<Picker>(*it);
    if (auto* oldLayer = picker.list_->parent()) {
        if (auto* oldHost = dynamic_cast<PopupHost*>(oldLayer->parent())) { oldHost->dismiss(); }
    }
    auto layer = std::make_shared<PopupLayer>(anchor);
    layer->add(picker.list_);
    add(layer);
    anchor_ = anchor;
    layer_ = std::move(layer);
    picker.set_expanded(true);
    on_layout_updated();
}
void PopupHost::dismiss() {
    if (!layer_) { return; }
    auto layer = std::move(layer_);
    auto picker = anchor_.lock();
    anchor_.reset();
    if (layer->parent() == this) { remove(*layer); }
    if (!layer->children().empty()) { layer->remove(*layer->children().front()); }
    if (picker) { picker->set_expanded(false); }
}
void PopupHost::on_layout_updated() {
    if (!layer_) { return; }
    auto picker = anchor_.lock();
    if (!picker || !picker->expanded() || picker->host() != this || !picker->interactive()) { dismiss(); return; }
    auto anchor = picker->viewport_bounds(true);
    auto view = viewport_bounds(true);
    if (anchor.width <= 0 || anchor.height <= 0) { dismiss(); return; }
    const auto origin = viewport_bounds();
    const float below = std::max(0.f, view.y + view.height - anchor.y - anchor.height);
    const float above = std::max(0.f, anchor.y - view.y);
    const float wanted = static_cast<float>(std::min(double(picker->maxHeight_),
        picker->list_->top(picker->list_->items().count)));
    const bool down = below >= wanted || below >= above;
    const float height = std::min(wanted, down ? below : above);
    if (height <= 0) { dismiss(); return; }
    auto& list = *picker->list_;
    const auto offset = list.scroll_offset();
    list.set_layout({ElementLayout::Kind::scroll, layout::Axis::vertical, 0, 1, height});
    list.set_scroll_offset(offset);
    list.set_preferred_size({anchor.width, height});
    list.set_offset({anchor.x - origin.x, anchor.y - origin.y + (down ? anchor.height : -height)});
}

Picker::Picker(std::wstring name, Items items, bool menu, float rowHeight, float maxHeight)
    : Pressable(name, menu ? UIA_MenuItemControlTypeId : UIA_ComboBoxControlTypeId),
      list_(new List(name, std::move(items), rowHeight, maxHeight, menu)),
      maxHeight_(maxHeight), menu_(menu) {
    if (!std::isfinite(maxHeight) || maxHeight <= 0) { throw std::invalid_argument("Expected positive popup height"); }
    list_->set_focusable(false);
    auto a = *accessibility();
    a.expansion = ElementAccessibility::Switch{false, [this](bool expanded) {
        if (expanded) { THROW_HR_IF(static_cast<HRESULT>(UIA_E_INVALIDOPERATION), !open()); }
        else { close(); }
    }};
    if (!menu) { a.value = ElementAccessibility::Value{}; }
    set_accessibility(std::move(a));
    selection_ = list_->on_selection([this](List::Index index) {
        if (expanded() && !preview_ && !menu_) { commit(index); }
    });
    activation_ = list_->on_activate([this](std::size_t index) {
        // The keyboard can rest on a heading, but choosing it does nothing: the popup stays open.
        if (!list_->selectable(index)) { return; }
        close();
        commit(index);
    });
}
PopupHost* Picker::host() const noexcept {
    for (auto* p = parent(); p; p = p->parent()) {
        if (auto* host = dynamic_cast<PopupHost*>(p)) { return host; }
    }
    return nullptr;
}
bool Picker::expanded() const noexcept { return accessibility()->expansion->on; }
void Picker::set_expanded(bool value) {
    if (value == expanded()) { return; }
    auto a = *accessibility();
    a.expansion->on = value;
    a.controllerFor.clear();
    if (value) { a.controllerFor.push_back(list_); }
    list_->popupFocus_ = value;
    set_accessibility(std::move(a));
}
bool Picker::open() {
    if (expanded()) { return true; }
    auto* target = host();
    if (!target || !list_->items().count || !interactive() || !request_focus()) { return false; }
    list_->prefix_.clear();
    list_->select(menu_ ? List::Index{} : selected_);
    if (!list_->selected()) { list_->navigate({VK_HOME, true}); }
    target->present(*this);
    update_host_layout();
    if (auto i = list_->selected()) { list_->reveal(*i); }
    update_host_layout();
    return expanded();
}
void Picker::close() {
    if (!expanded()) { return; }
    if (auto* target = host(); target && target->anchor_.lock().get() == this) { target->dismiss(); }
    else { set_expanded(false); }
}
void Picker::on_focus_changed(bool gained) {
    Pressable::on_focus_changed(gained);
    if (!gained) { close(); }
}
void Picker::on_detaching() noexcept {
    // The host may be walking its children during teardown. Its next layout removes the layer.
    try { set_expanded(false); } catch (...) { LOG_CAUGHT_EXCEPTION(); }
}
void Picker::commit(List::Index index) {
    if (index && !list_->selectable(*index)) { return; }
    if (index == selected_) { return; }
    auto text = index ? list_->items().label(*index) : std::wstring{};
    selected_ = index;
    selectedText_ = std::move(text);
    if (!menu_) { set_accessible_value(selectedText_); }
    invalidate();
    changed_.emit(index);
}
void Picker::set_selected(List::Index index) {
    if (list_->select(index) && list_->selected() == index) { commit(index); }
}
void Picker::set_items(Items items) {
    close();
    list_->set_items(std::move(items));
    commit({});
}
void Picker::activate() { if (expanded()) { close(); } else { open(); } }
bool Picker::on_text_input(std::wstring_view text) {
    if (!expanded() && menu_) { return false; }
    preview_ = expanded();
    const auto reset = wil::scope_exit([&] { preview_ = false; });
    if (!list_->type_ahead(text)) { return false; }
    if (!expanded()) { commit(list_->selected()); }
    return true;
}
bool Picker::on_key(const KeyEvent& e) {
    if (!focused()) { return false; }
    if (e.down && (e.key == VK_F4 || (e.alt && (e.key == VK_UP || e.key == VK_DOWN)))) {
        if (!(e.keyData & (1L << 30))) { activate(); }
        return true;
    }
    if (expanded()) {
        if (e.down && (e.key == VK_ESCAPE || e.key == VK_TAB)) { close(); return e.key == VK_ESCAPE; }
        if (e.key == VK_RETURN || e.key == VK_SPACE) {
            if (e.down && !(e.keyData & (1L << 30))) { list_->activate_selected(); }
            return true;
        }
        preview_ = true;
        const auto reset = wil::scope_exit([&] { preview_ = false; });
        return list_->navigate(e);
    }
    if (e.down && !e.alt && (e.key == VK_DOWN || e.key == VK_UP)) { return open(); }
    return Pressable::on_key(e);
}
}
