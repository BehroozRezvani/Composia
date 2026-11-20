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
        a.invoke = [this] { activate(); };
        set_accessibility(std::move(a));
        if (list.items_.command) {
            command = list.items_.command(i);
            if (command) {
                set_enabled(command->enabled.get());
                availability = command->enabled.on_change([this](bool) { set_enabled(command->enabled.get()); });
            }
        }
        set_offset({0, static_cast<float>(double(i) * list.rowHeight_)});
    }
    void activate() override {
        if (owner && owner->select(index) && owner) { owner->activate_selected(); }
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
    // Positions are single-precision DIPs. Reject an extent at which adjacent rows coincide.
    const double extent = double(items.count) * rowHeight_;
    if (extent > std::numeric_limits<float>::max() ||
        static_cast<float>(extent) - std::nextafter(static_cast<float>(extent), 0.f) > rowHeight_) {
        throw std::length_error("List exceeds the drawing coordinate range");
    }
    while (!rows_->children().empty()) {
        auto row = rows_->children().back();
        static_cast<Row&>(*row).owner = nullptr;
        rows_->remove(*row);
    }
    const bool notify = selected_.has_value();
    selected_.reset();
    items_ = std::move(items);
    rows_->set_preferred_size({0, static_cast<float>(extent)});
    set_scroll_offset(0);
    invalidate_measure();
    if (notify) { changed_.emit({}); }
}
void List::set_row_renderer(RowRenderer renderer) { renderer_ = std::move(renderer); invalidate(); }
std::size_t List::realized_count() const noexcept { return rows_->children().size(); }

List::Row& List::realize(std::size_t index) {
    auto rows = rows_->children();
    auto it = std::lower_bound(rows.begin(), rows.end(), index, [](const auto& child, auto i) {
        return static_cast<Row&>(*child).index < i;
    });
    if (it != rows.end() && static_cast<Row&>(**it).index == index) { return static_cast<Row&>(**it); }
    const auto at = static_cast<std::size_t>(it - rows.begin());
    auto row = std::make_shared<Row>(*this, index);
    row->set_preferred_size({bounds().width, rowHeight_});
    rows_->insert(at, row);
    return *row;
}
void List::on_layout_updated() {
    const auto start = std::min(items_.count, static_cast<std::size_t>(double(scroll_offset()) / rowHeight_));
    const auto end = static_cast<std::size_t>(std::min(double(items_.count), std::ceil(
        (double(scroll_offset()) + bounds().height) / rowHeight_)));
    for (auto n = realized_count(); n > 0; --n) {
        auto& row = static_cast<Row&>(*rows_->children()[n - 1]);
        if (selected_ != row.index && (row.index < start || row.index >= end)) {
            row.owner = nullptr;
            rows_->remove(row);
        } else { row.set_preferred_size({bounds().width, rowHeight_}); }
    }
    for (auto i = start; i < end; ++i) { realize(i); }
}
void List::synchronize_selection() {
    Index next;
    for (const auto& child : rows_->children()) {
        auto& row = static_cast<Row&>(*child);
        if (row.state().selected) { next = row.index; break; }
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
    const auto y = static_cast<float>(double(index) * rowHeight_);
    if (y < scroll_offset()) { set_scroll_offset(y); }
    else if (y + rowHeight_ > scroll_offset() + bounds().height) {
        set_scroll_offset(y + std::min(rowHeight_, bounds().height) - bounds().height);
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
bool List::navigate(const KeyEvent& e) {
    if (!interactive() || !e.down || e.alt || e.control || e.shift || !items_.count) { return false; }
    if (e.key == VK_RETURN || e.key == VK_SPACE) { return activate_selected(); }
    const auto last = items_.count - 1;
    const auto page = std::max<std::size_t>(1, static_cast<std::size_t>(
        std::min(double(items_.count), double(bounds().height) / rowHeight_)));
    const bool backwards = e.key == VK_UP || e.key == VK_PRIOR || e.key == VK_END;
    std::size_t candidate{};
    switch (e.key) {
    case VK_HOME: candidate = 0; break;
    case VK_END: candidate = last; break;
    case VK_DOWN: case VK_NEXT: {
        const auto step = e.key == VK_NEXT ? page : 1;
        candidate = selected_ ? *selected_ + std::min(last - *selected_, step) : 0;
        break;
    }
    case VK_UP: case VK_PRIOR: {
        const auto step = e.key == VK_PRIOR ? page : 1;
        candidate = selected_ ? *selected_ - std::min(*selected_, step) : last;
        break;
    }
    default: return false;
    }
    for (;;) {
        auto command = items_.command ? items_.command(candidate) : nullptr;
        if ((!command || command->enabled.get()) && select(candidate)) {
            reveal(candidate);
            return true;
        }
        if (backwards ? candidate == 0 : candidate == last) { return true; }
        if (backwards) { --candidate; } else { ++candidate; }
    }
}
}
