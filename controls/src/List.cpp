#include <composia/Controls.hpp>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace composia::controls {
Items Items::strings(std::vector<std::wstring> labels) {
    auto data = std::make_shared<const std::vector<std::wstring>>(std::move(labels));
    return {data->size(), [data](std::size_t i) { return data->at(i); }};
}

class List::Row final : public Pressable {
public:
    Row(List& list, std::size_t i)
        : Pressable(list.items_.label(i), list.menu_ ? UIA_MenuItemControlTypeId : UIA_ListItemControlTypeId),
          owner(&list), index(i) {
        set_focusable(false);
        auto a = *accessibility();
        a.selected = false;
        a.invoke = [this] { choose(true); };
        set_accessibility(std::move(a));
        if (list.items_.command) {
            command = list.items_.command(i);
            if (command) {
                set_available(command->enabled.get());
                availability = command->enabled.on_change([this](bool) { set_available(command->enabled.get()); });
            }
        }
        set_offset({0, static_cast<float>(list.top(i))});
    }
    void activate() override {
        choose(doubleClick || (owner && owner->popupFocus_));
    }
    bool on_pointer(const PointerEvent& e) override {
        if (e.kind == PointerEvent::Kind::down) { doubleClick = e.doubleClick; }
        return Pressable::on_pointer(e);
    }
    void choose(bool invoke) {
        if (owner && owner->select(index) && owner && owner->selected() == index && invoke) {
            owner->activate_selected();
        }
    }
    void paint(ScopedSurfaceDraw& draw) override {
        if (!owner || !owner->renderer_) { return; }
        auto s = state();
        s.focused = s.selected && (owner->focused() || owner->popupFocus_);
        auto b = bounds();
        owner->renderer_(draw, index, name(), {b.width, b.height}, s, owner->tokens());
    }
    List* owner;
    const std::size_t index;
    std::shared_ptr<Command> command;
    Connection availability;
    bool doubleClick{};
};

List::List(std::wstring name, Items items, float rowHeight, float height)
    : List(std::move(name), std::move(items), rowHeight, height, false) {}
List::List(std::wstring name, Items items, float rowHeight, float height, bool menu)
    : ScrollView(std::move(name), layout::Axis::vertical, height),
      rows_(std::make_shared<Element>(ElementLayout{ElementLayout::Kind::canvas})), rowHeight_(rowHeight), menu_(menu) {
    if (!std::isfinite(rowHeight) || rowHeight <= 0) { throw std::invalid_argument("Expected a positive row height"); }
    auto a = *accessibility();
    a.controlType = menu ? UIA_MenuControlTypeId : UIA_ListControlTypeId;
    a.selection = ElementAccessibility::Selection{false, false, [this] { synchronize_selection(); }};
    set_accessibility(std::move(a));
    set_content(rows_);
    observe_layout();
    set_items(std::move(items));
}
List::~List() {
    for (auto& child : rows_->children()) { static_cast<Row&>(*child).owner = nullptr; }
}
void List::set_items(Items items) {
    if (items.count && !items.label) { throw std::invalid_argument("A list needs row labels"); }
    std::vector<double> tops;
    float shortest = rowHeight_;
    double extent = double(items.count) * rowHeight_;
    if (items.height) {
        tops.reserve(items.count + 1);
        extent = 0;
        for (std::size_t i = 0; i < items.count; ++i) {
            const float height = items.height(i);
            if (!std::isfinite(height) || height <= 0) { throw std::invalid_argument("Expected a positive row height"); }
            tops.push_back(extent);
            extent += height;
            shortest = i ? std::min(shortest, height) : height;
        }
        tops.push_back(extent);
    }
    // Positions are single-precision DIPs. Reject an extent at which adjacent rows coincide.
    if (extent > std::numeric_limits<float>::max() ||
        static_cast<float>(extent) - std::nextafter(static_cast<float>(extent), 0.f) > shortest) {
        throw std::length_error("List exceeds the drawing coordinate range");
    }
    while (!rows_->children().empty()) {
        auto row = rows_->children().back();
        static_cast<Row&>(*row).owner = nullptr;
        rows_->remove(*row);
    }
    const bool notify = selected_.has_value();
    selected_.reset();
    prefix_.clear();
    items_ = std::move(items);
    tops_ = std::move(tops);
    rows_->set_preferred_size({0, static_cast<float>(extent)});
    set_scroll_offset(0);
    invalidate_measure();
    if (notify) { changed_.emit({}); }
}
void List::set_row_renderer(RowRenderer renderer) { renderer_ = std::move(renderer); invalidate(); }
std::size_t List::realized_count() const noexcept { return rows_->children().size(); }

double List::top(std::size_t index) const noexcept {
    return tops_.empty() ? double(index) * rowHeight_ : tops_[index];
}
float List::row_height(std::size_t index) const noexcept {
    return tops_.empty() ? rowHeight_ : static_cast<float>(tops_[index + 1] - tops_[index]);
}
std::size_t List::rows_before(double y) const noexcept {
    if (y <= 0) { return 0; }
    if (tops_.empty()) { return static_cast<std::size_t>(std::min(double(items_.count), std::ceil(y / rowHeight_))); }
    return static_cast<std::size_t>(std::lower_bound(tops_.begin(), tops_.end() - 1, y) - tops_.begin());
}
List::Index List::row_at(double y) const noexcept {
    if (y < 0 || y >= top(items_.count)) { return std::nullopt; }
    if (tops_.empty()) { return std::min(items_.count - 1, static_cast<std::size_t>(y / rowHeight_)); }
    return static_cast<std::size_t>(std::upper_bound(tops_.begin(), tops_.end() - 1, y) - tops_.begin()) - 1;
}

List::Row& List::realize(std::size_t index) {
    auto rows = rows_->children();
    auto it = std::lower_bound(rows.begin(), rows.end(), index, [](const auto& child, auto i) {
        return static_cast<Row&>(*child).index < i;
    });
    if (it != rows.end() && static_cast<Row&>(**it).index == index) { return static_cast<Row&>(**it); }
    const auto at = static_cast<std::size_t>(it - rows.begin());
    auto row = std::make_shared<Row>(*this, index);
    row->set_preferred_size({bounds().width, row_height(index)});
    rows_->insert(at, row);
    return *row;
}
void List::on_layout_updated() {
    // The rows from the one at the top of the viewport to the last that starts inside it.
    const double offset = scroll_offset();
    const auto start = row_at(offset).value_or(items_.count);
    const auto end = rows_before(offset + bounds().height);
    for (auto n = realized_count(); n > 0; --n) {
        auto& row = static_cast<Row&>(*rows_->children()[n - 1]);
        if (selected_ != row.index && (row.index < start || row.index >= end)) {
            row.owner = nullptr;
            rows_->remove(row);
        } else { row.set_preferred_size({bounds().width, row_height(row.index)}); }
    }
    for (auto i = start; i < end; ++i) { realize(i); }
}
bool List::on_context_menu(const ContextMenuEvent& e) {
    auto event = e;
    if (!e.keyboard) {
        if (const auto row = row_at(double(e.position.y) + scroll_offset())) { select(*row); }
    } else if (selected_) {
        event.position = {0, static_cast<float>(top(*selected_ + 1)) - scroll_offset()};
    }
    return Control::on_context_menu(event);
}
void List::synchronize_selection() {
    Index next;
    for (const auto& child : rows_->children()) {
        auto& row = static_cast<Row&>(*child);
        if (row.accessibility()->selected.value_or(false)) { next = row.index; break; }
    }
    if (next == selected_) { return; }
    selected_ = next;
    invalidate_measure();
    changed_.emit(next);
}
bool List::select(Index index) {
    if (index && *index >= items_.count) { throw std::out_of_range("Invalid list index"); }
    if (index == selected_) { return true; }
    if (!index) { return realize(*selected_).select_item(SelectionAction::remove); }
    return realize(*index).select_item(SelectionAction::select);
}
void List::reveal(std::size_t index) {
    if (index >= items_.count) { throw std::out_of_range("Invalid list index"); }
    const auto y = static_cast<float>(top(index));
    const auto height = row_height(index);
    if (y < scroll_offset()) { set_scroll_offset(y); }
    else if (y + height > scroll_offset() + bounds().height) {
        set_scroll_offset(y + std::min(height, bounds().height) - bounds().height);
    }
}
bool List::activate_selected() {
    if (!selected_ || !interactive()) { return false; }
    const auto index = *selected_;
    auto& row = realize(index);
    if (!row.interactive()) { return false; }
    auto command = row.command;
    activated_.emit(index);
    if (command) { command->execute(); }
    return true;
}
bool List::type_ahead(std::wstring_view text) {
    if (!interactive() || text.empty() || !items_.count || text.front() < L' ') { return false; }
    const auto now = GetTickCount64();
    if (now - lastText_ > 1000) { prefix_.clear(); }
    if (prefix_.empty() && text == L" ") { return false; }
    const bool cycle = text.size() == 1 && prefix_ == text;
    const bool fresh = prefix_.empty() || cycle;
    if (!cycle) { prefix_.append(text); }
    lastText_ = now;
    const auto start = selected_ ? (*selected_ + (fresh ? 1 : 0)) % items_.count : 0;
    if (items_.findPrefix) {
        auto match = items_.findPrefix(prefix_, start);
        if (match && select(match) && selected_ == match) { reveal(*match); }
    } else {
        auto i = start;
        do {
            auto label = items_.label(i);
            if (label.size() >= prefix_.size() && prefix_.size() <= INT_MAX &&
                CompareStringOrdinal(label.data(), static_cast<int>(prefix_.size()), prefix_.data(),
                                     static_cast<int>(prefix_.size()), TRUE) == CSTR_EQUAL && select(i)) {
                if (selected_ == i) { reveal(i); }
                break;
            }
            i = (i + 1) % items_.count;
        } while (i != start);
    }
    return true;
}
bool List::navigate(const KeyEvent& e) {
    if (!interactive() || !e.down || e.alt || e.control || e.shift || !items_.count) { return false; }
    if (e.key == VK_RETURN || e.key == VK_SPACE) { return activate_selected(); }
    const auto last = items_.count - 1;
    const bool backwards = e.key == VK_UP || e.key == VK_PRIOR || e.key == VK_END;
    std::size_t candidate{};
    switch (e.key) {
    case VK_HOME: candidate = 0; break;
    case VK_END: candidate = last; break;
    case VK_DOWN: candidate = selected_ ? std::min(last, *selected_ + 1) : 0; break;
    case VK_UP: candidate = selected_ ? *selected_ - std::min<std::size_t>(*selected_, 1) : last; break;
    case VK_NEXT:
        // A page on: the row a viewport's height below the top of this one, at least the next.
        candidate = !selected_ ? 0 : std::min(last, std::max(*selected_ + 1,
            row_at(top(*selected_) + bounds().height).value_or(last)));
        break;
    case VK_PRIOR:
        // A page back: the first row that starts within a viewport's height above this one.
        candidate = !selected_ ? last : std::min(*selected_ - std::min<std::size_t>(*selected_, 1),
            rows_before(top(*selected_) - bounds().height));
        break;
    default: return false;
    }
    for (;;) {
        auto command = items_.command ? items_.command(candidate) : nullptr;
        if ((!command || command->enabled.get()) && select(candidate)) {
            if (selected_ == candidate) { reveal(candidate); }
            return true;
        }
        if (backwards ? candidate == 0 : candidate == last) { return true; }
        if (backwards) { --candidate; } else { ++candidate; }
    }
}
}
