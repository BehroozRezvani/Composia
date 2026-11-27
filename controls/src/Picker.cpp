#include <composia/Controls.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace composia::controls {
namespace {
bool light(std::uint32_t rgb) {
    const auto r = (rgb >> 16) & 0xff, g = (rgb >> 8) & 0xff, b = rgb & 0xff;
    return 0.2126 * r + 0.7152 * g + 0.0722 * b > 0.5 * 255;
}

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
      list_(std::make_shared<List>(name, std::move(items), rowHeight, maxHeight, menu ? ListRole::menu : ListRole::list)),
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
    // A trigger that moves while its list is open in a window closes it.
    observe_layout();
    activation_ = list_->on_activate([this](std::size_t index) {
        // The keyboard can rest on a heading, but choosing it does nothing: the popup stays open.
        if (!list_->selectable(index)) { return; }
        // A row that toggles in place, such as a menu's option, leaves the popup open.
        if (const auto& items = list_->items(); items.staysOpen && items.staysOpen(index)) {
            commit(index, true);
            return;
        }
        close();
        // A menu's choice is an action, so the same row again counts; a dropdown's is a value.
        commit(index, menu_);
    });
}
Picker::~Picker() {
    *alive_ = false;
    release_window();
}
void Picker::release_window() noexcept {
    if (!window_) { return; }
    try {
        windowClosed_.disconnect();
        if (window_->is_open()) { window_->close(); }
        if (scope_ && list_->parent() == scope_.get()) { scope_->remove(*list_); }
        // A choice in the popup may be destroying this picker from the popup's own message.
        auto& application = window_->application();
        application.retire(std::move(window_));
    } catch (...) { LOG_CAUGHT_EXCEPTION(); }
    window_.reset();
    scope_.reset();
    list_->cueWindow_ = nullptr;
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
    auto* window = target ? nullptr : host_window();
    if ((!target && !window) || !list_->items().count || !interactive() || !request_focus()) { return false; }
    list_->prefix_.clear();
    list_->select(menu_ ? List::Index{} : selected_);
    if (!list_->selected()) { list_->navigate({VK_HOME, true}); }
    if (!target) { return open_window(*window); }
    target->present(*this);
    update_host_layout();
    if (auto i = list_->selected()) { list_->reveal(*i); }
    update_host_layout();
    return expanded();
}
RECT Picker::screen_anchor(const ElementWindow& owner) const {
    const auto box = viewport_bounds(true);
    const float scale = owner.scale();
    RECT rect{std::lround(box.x * scale), std::lround(box.y * scale), std::lround((box.x + box.width) * scale),
        std::lround((box.y + box.height) * scale)};
    MapWindowPoints(owner.hwnd(), nullptr, reinterpret_cast<POINT*>(&rect), 2);
    return rect;
}
bool Picker::open_window(ElementWindow& owner) {
    const auto box = viewport_bounds(true);
    if (box.width <= 0 || box.height <= 0) { return false; }
    if (window_ && (!window_->hwnd() || window_->owner_hwnd() != GetAncestor(owner.hwnd(), GA_ROOT))) { release_window(); }
    const auto palette = tokens();
    if (!window_) {
        scope_ = std::make_shared<StyleScope>(palette);
        PopupWindow::Options options;
        // The trigger keeps the keyboard and drives the list, as it does in a PopupHost.
        options.activation = PopupWindow::Activation::keep_owner_focus;
        options.background = palette.background;
        options.menu = menu_;
        window_ = std::make_unique<PopupWindow>(owner, scope_, name(), options);
        windowClosed_ = window_->on_closed([this, alive = alive_](PopupWindow::CloseReason) {
            if (*alive) { window_closed(); }
        });
    }
    scope_->set_tokens(palette);
    window_->set_background(palette.background);
    window_->set_dark(!light(palette.background));
    if (auto* parent = list_->parent()) { parent->remove(*list_); }
    const float height = static_cast<float>(std::min(double(maxHeight_), list_->top(list_->items().count)));
    const auto offset = list_->scroll_offset();
    list_->set_layout({ElementLayout::Kind::scroll, layout::Axis::vertical, 0, 1, height});
    list_->set_scroll_offset(offset);
    list_->set_offset({});
    list_->set_preferred_size({box.width, height});
    scope_->add(list_);
    list_->cueWindow_ = &owner;
    anchor_ = screen_anchor(owner);
    window_->show_below(anchor_, {box.width, height}, PopupWindow::Below::match_width);
    if (!window_ || !window_->is_open()) { return false; }
    set_expanded(true);
    if (auto i = list_->selected()) { list_->reveal(*i); }
    return expanded();
}
void Picker::window_closed() noexcept {
    try {
        list_->cueWindow_ = nullptr;
        set_expanded(false);
        if (scope_ && list_->parent() == scope_.get()) { scope_->remove(*list_); }
    } catch (...) { LOG_CAUGHT_EXCEPTION(); }
}
void Picker::on_layout_updated() {
    if (!window_ || !window_->is_open()) { return; }
    auto* owner = host_window();
    const auto box = viewport_bounds(true);
    if (!owner || !interactive() || box.width <= 0 || box.height <= 0) { close(); return; }
    const auto anchor = screen_anchor(*owner);
    if (!EqualRect(&anchor, &anchor_)) { close(); }
}
void Picker::close() {
    if (!expanded()) { return; }
    if (window_ && window_->is_open()) {
        window_->close();
        return;
    }
    if (auto* target = host(); target && target->anchor_.lock().get() == this) { target->dismiss(); }
    else { set_expanded(false); }
}
void Picker::on_focus_changed(bool gained) {
    Pressable::on_focus_changed(gained);
    if (!gained) { close(); }
}
void Picker::on_detaching() noexcept {
    // The host may be walking its children during teardown. Its next layout removes the layer. A
    // popup window never had the keyboard, so hiding it moves nothing.
    try {
        list_->cueWindow_ = nullptr;
        if (window_ && window_->is_open()) { window_->close(); }
        set_expanded(false);
    } catch (...) { LOG_CAUGHT_EXCEPTION(); }
}
void Picker::commit(List::Index index, bool always) {
    if (index && !list_->selectable(*index)) { return; }
    if (index == selected_ && !always) { return; }
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
void Picker::activate() {
    if (ignoreActivation_) { return; }
    if (expanded()) { close(); } else { open(); }
}
bool Picker::on_pointer(const PointerEvent& e) {
    // The press that closed the popup window over the trigger does not open it again on release.
    ignoreActivation_ = e.kind == PointerEvent::Kind::up && window_ && window_->closed_by_anchor_press();
    const auto reset = wil::scope_exit([&] { ignoreActivation_ = false; });
    return Pressable::on_pointer(e);
}
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
