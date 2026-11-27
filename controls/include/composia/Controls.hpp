#pragma once
#include <composia/Appearance.hpp>
#include <composia/ElementAccessibility.hpp>
#include <composia/TextBox.hpp>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <utility>

namespace composia {
class HostedVisual;
}

namespace composia::controls {

// UI-thread state. Each observer receives the value of that particular change, even on reentry.
template<class T> class Observable {
public:
    explicit Observable(T initial = {}) : value_(std::move(initial)) {}
    Observable(const Observable&) = delete;
    Observable& operator=(const Observable&) = delete;
    [[nodiscard]] const T& get() const noexcept { return value_; }
    void set(T next) {
        if (value_ == next) { return; }
        value_ = next;
        changed_.emit(next);
    }
    Connection on_change(std::function<void(const T&)> f) { return changed_.connect(std::move(f)); }
private:
    T value_;
    Signal<const T&> changed_;
};

class Command {
public:
    explicit Command(std::function<void()>);
    Observable<bool> enabled{true};
    bool execute() const;
private:
    std::function<void()> execute_;
};

// focusVisible: focused while the window shows keyboard focus (Window::focus_visible), when a
// renderer draws its focus ring.
struct State { bool hovered{}, pressed{}, focused{}, disabled{}, selected{}, focusVisible{}; };
// The colors and metrics a renderer draws with. Colors are 0xRRGGBB.
struct StyleTokens {
    std::uint32_t foreground{0x202020}, background{0xffffff}, accent{0x0067c0};
    float padding{6}, radius{4};
    std::uint32_t accentForeground{0xffffff};         // Text and glyphs on the accent.
    std::uint32_t hover{0xf0f0f0}, pressed{0xe4e4e4}; // Backgrounds under the pointer and while pressed.
    std::uint32_t border{0x8a8a8a}, focus{0x000000};  // Outlines, and the keyboard focus ring.
    std::uint32_t disabled{0x9e9e9e};                 // Text and glyphs of a disabled control.
    float fontSize{14};                               // DIPs.
    bool operator==(const StyleTokens&) const = default;

    // A starting point for an application that follows the system appearance: light or dark
    // neutrals around its accent, or under high contrast the theme's own colors, with the font
    // size scaled by the text size setting. Controls use it only when the application passes it.
    [[nodiscard]] static StyleTokens from(const Appearance&);
};

// Gives the controls inside it that have no tokens of their own the same tokens, such as an
// application's theme. A scope without tokens passes on what it inherits.
class StyleScope : public Element {
public:
    explicit StyleScope(std::optional<StyleTokens> tokens = std::nullopt, ElementLayout layout = {});
    void set_tokens(std::optional<StyleTokens>);
    [[nodiscard]] const std::optional<StyleTokens>& tokens() const noexcept { return tokens_; }
private:
    std::optional<StyleTokens> tokens_;
};

using TextBox = composia::TextBox;
[[nodiscard]] State state_of(const TextBox&);

// A control draws only through its renderer, which receives its tokens: its own, else those of
// the nearest ancestor control or StyleScope that has some, else StyleTokens{}.
class Control : public Element {
public:
    using Renderer = std::function<void(ScopedSurfaceDraw&, const Control&, State, const StyleTokens&)>;
    void set_renderer(Renderer);
    // nullopt inherits tokens again.
    void set_tokens(std::optional<StyleTokens>);
    [[nodiscard]] StyleTokens tokens() const;
    [[nodiscard]] const std::optional<StyleTokens>& own_tokens() const noexcept { return tokens_; }
    [[nodiscard]] virtual State state() const;
    [[nodiscard]] std::wstring_view name() const noexcept;
    // A context menu request on the control or inside it. While a callback is connected, the
    // control handles the request, so it goes no further up.
    Connection on_context_requested(std::function<void(const ContextMenuEvent&)> f) { return contextMenu_.connect(std::move(f)); }
protected:
    Control(std::wstring name, long role, ElementLayout = {});
    bool on_context_menu(const ContextMenuEvent&) override;
    void paint(ScopedSurfaceDraw&) override;
    void on_hover_changed(bool) override { invalidate(); }
    void on_capture_lost() noexcept override;
    void on_focus_changed(bool) override;
    void set_pressed(bool);
private:
    bool pressed_{};
    Renderer renderer_;
    std::optional<StyleTokens> tokens_;
    Signal<const ContextMenuEvent&> contextMenu_;
};

class Pressable : public Control {
protected:
    using Control::Control;
    virtual void activate() = 0;
    bool on_pointer(const PointerEvent&) override;
    bool on_key(const KeyEvent&) override;
    void on_capture_lost() noexcept override;
    void on_hover_changed(bool) override;
private:
    bool pointerArmed_{};
};

class Button final : public Pressable {
public:
    Button(std::wstring name, std::shared_ptr<Command>);
    Button(std::wstring name, std::function<void()>);
private:
    void activate() override;
    std::shared_ptr<Command> command_;
    Connection availability_;
};

class Toggle final : public Pressable {
public:
    explicit Toggle(std::wstring name, bool checked = false);
    [[nodiscard]] bool checked() const noexcept;
    void set_checked(bool);
    Connection on_change(std::function<void(bool)> f) { return changed_.connect(std::move(f)); }
private:
    void activate() override;
    Signal<bool> changed_;
};

class Slider final : public Control {
public:
    Slider(std::wstring name, double minimum, double maximum, double value,
           double smallChange = 1, double largeChange = 10, layout::Axis = layout::Axis::horizontal);
    [[nodiscard]] double value() const noexcept;
    void set_value(double);
    [[nodiscard]] layout::Axis axis() const noexcept { return axis_; }
    Connection on_change(std::function<void(double)> f) { return changed_.connect(std::move(f)); }
private:
    bool on_pointer(const PointerEvent&) override;
    layout::Axis axis_;
    Signal<double> changed_;
};

// Scrolls its content on one axis. While the content overflows, a scrollbar lies over its far
// edge: it widens under the pointer, its thumb can be dragged, and a press on its track pages
// towards the press. A scrollbar renderer draws it; without one it is a thin bar in the
// foreground color.
class ScrollView : public Control {
public:
    // The scrollbar as its renderer draws it, in the view's local DIPs: the track and the thumb
    // at the current thickness, wider while hovered or dragged.
    struct Scrollbar {
        layout::Rect track{}, thumb{};
        bool hovered{}, dragging{};
    };
    using ScrollbarRenderer = std::function<void(ScopedSurfaceDraw&, const ScrollView&, const Scrollbar&, const StyleTokens&)>;

    explicit ScrollView(std::wstring name, layout::Axis = layout::Axis::vertical, float viewportLength = 160);
    void set_content(std::shared_ptr<Element>);
    // An empty renderer restores the thin bar.
    void set_scrollbar_renderer(ScrollbarRenderer);
    // Hides or shows the scrollbar; scrolling by wheel, keyboard and automation is unaffected.
    void set_scrollbar(bool visible);
    [[nodiscard]] bool scrollbar() const noexcept { return scrollbar_; }
protected:
    bool on_key(const KeyEvent&) override;
    bool on_pointer(const PointerEvent&) override;
    void on_hover_changed(bool) override;
    void on_capture_lost() noexcept override;
    void paint_over(ScopedSurfaceDraw&) override;
    [[nodiscard]] bool claims_pointer(layout::Point) const override;
private:
    struct Thumb { float start, length, track; };
    [[nodiscard]] std::optional<Thumb> thumb() const;
    void set_bar_hovered(bool);
    ScrollbarRenderer scrollbarRenderer_;
    layout::Axis axis_;
    bool scrollbar_{true}, barHovered_{}, dragging_{};
    float grab_{};
};

struct Items {
    std::size_t count{};
    std::function<std::wstring(std::size_t)> label;
    std::function<std::shared_ptr<Command>(std::size_t)> command{};
    // Optional indexed, case-insensitive search from start, wrapping at the end.
    std::function<std::optional<std::size_t>(std::wstring_view prefix, std::size_t start)> findPrefix{};
    // Optional height of each row in DIPs, positive and finite. Without it, every row takes the
    // list's row height. The list reads every height when the items are set.
    std::function<float(std::size_t)> height{};
    // Optional: whether a row can be selected. A row that cannot, such as a heading, can still be
    // the current row, where the keyboard acts, but is never selected.
    std::function<bool(std::size_t)> selectable{};
    // Optional depth of each row in a tree, from 0 for the top level, with each row's children
    // after it. A list with levels or expansion states is a tree.
    std::function<std::size_t(std::size_t)> level{};
    // Optional: whether a row's children show (true), are folded away (false), or it has none.
    std::function<std::optional<bool>(std::size_t)> expanded{};
    // Optional elements over a row, made each time the row is realized and filling it: controls
    // inside a row, such as a button. They draw above the row renderer and take pointer input
    // before the row; what they leave goes on to the row.
    std::function<std::shared_ptr<Element>(std::size_t)> content{};
    // Optional: whether a row is a separator, a line between groups of rows. It is never selected,
    // current or chosen: the keyboard, type-ahead and the pointer pass it by. UI Automation sees a
    // Separator, which is not content. Without `height` it is List::separatorHeight tall. The list
    // reads every row's flag when the items are set.
    std::function<bool(std::size_t)> separator{};
    static Items strings(std::vector<std::wstring>);
};

class List final : public ScrollView {
public:
    using Index = std::optional<std::size_t>;
    using RowRenderer = std::function<void(ScopedSurfaceDraw&, std::size_t, std::wstring_view,
                                            layout::Size, State, const StyleTokens&)>;
    using SeparatorRenderer = std::function<void(ScopedSurfaceDraw&, std::size_t, layout::Size, const StyleTokens&)>;
    static constexpr float separatorHeight = 9;
    List(std::wstring name, Items, float rowHeight = 32, float viewportHeight = 160);
    ~List() override;
    // New rows: the selection, the current row and the scroll position start over.
    void set_items(Items);
    // New rows for the same view, as when a tree row opens: the scroll position stays where it
    // can, the given row is selected (or none), and the keyboard rests on `current` if given,
    // else on the selected row. Raises no selection change; the rows are realized again.
    void replace_items(Items, Index selected, Index current = std::nullopt);
    void set_row_renderer(RowRenderer);
    // Draws separator rows; an empty renderer restores the default, a line in the border color
    // across the middle, inset by the padding.
    void set_separator_renderer(SeparatorRenderer);
    [[nodiscard]] const Items& items() const noexcept { return items_; }
    [[nodiscard]] Index selected() const noexcept { return selected_; }
    // The row the keyboard acts on: the selected row, or a row that cannot be selected once the
    // keyboard or the pointer moved to it.
    [[nodiscard]] Index current() const noexcept { return current_; }
    // False for a row that cannot be selected.
    bool select(Index);
    // Moves to a row as the arrow keys do: selects it, or makes a row that cannot be selected the
    // current one, and reveals it.
    bool go_to(std::size_t);
    void reveal(std::size_t);
    // The row at a point in the list's own DIPs, as its pointer and context menu events give
    // them, or nullopt.
    [[nodiscard]] Index index_at(layout::Point) const noexcept;
    // Reads a realized row's label again and repaints it, for a change that keeps its height,
    // such as a count, or a look the renderer gives it, such as a drop target.
    void refresh(std::size_t);
    // Lays Composition content over all the rows, in their coordinates: it scrolls with them and
    // spans the whole list, for decorations across rows, such as an indicator that glides from
    // row to row. Null removes it.
    void set_overlay(std::shared_ptr<HostedVisual>);
    // Where a row is in the overlay's coordinates, realized or not.
    [[nodiscard]] layout::Rect row_bounds(std::size_t) const;
    bool navigate(const KeyEvent&);
    bool type_ahead(std::wstring_view);
    // Activates the current row: the selected one, unless the keyboard rests on a row that
    // cannot be selected.
    bool activate_selected();
    [[nodiscard]] std::size_t realized_count() const noexcept;
    Connection on_selection(std::function<void(Index)> f) { return changed_.connect(std::move(f)); }
    Connection on_activate(std::function<void(std::size_t)> f) { return activated_.connect(std::move(f)); }
    // In a tree, asks to show (true) or fold (false) a row's children: Right on a folded row,
    // Left on an open one, or UI Automation's ExpandCollapse. The list changes nothing itself;
    // the owner updates its rows and sets them again.
    Connection on_expand(std::function<void(std::size_t, bool)> f) { return expand_.connect(std::move(f)); }
private:
    friend class Picker;
    friend class PopupHost;
    class Row;
    List(std::wstring, Items, float, float, bool menu);
    void assign(Items, Index selected, Index current, bool keepView);
    Row& realize(std::size_t);
    // Where row i starts in the content, and how tall it is, in DIPs; top(count) is the extent.
    [[nodiscard]] double top(std::size_t) const noexcept;
    [[nodiscard]] float row_height(std::size_t) const noexcept;
    // How many rows start before the content offset y.
    [[nodiscard]] std::size_t rows_before(double y) const noexcept;
    // The row at the content offset y, or nullopt past either end.
    [[nodiscard]] Index row_at(double y) const noexcept;
    [[nodiscard]] bool selectable(std::size_t) const;
    [[nodiscard]] bool separator(std::size_t i) const noexcept { return std::ranges::binary_search(separators_, i); }
    // A row's place among its peers, from 1, and their number, for UI Automation: among the rows
    // of a flat list, or the rows with the same parent in a tree, separators left out. 0 and 0
    // for a separator.
    [[nodiscard]] std::pair<int, int> place_in_set(std::size_t) const;
    [[nodiscard]] bool tree() const noexcept { return items_.level || items_.expanded; }
    [[nodiscard]] std::size_t level(std::size_t i) const { return items_.level ? items_.level(i) : 0; }
    [[nodiscard]] std::optional<bool> expansion(std::size_t i) const {
        return items_.expanded ? items_.expanded(i) : std::nullopt;
    }
    // Right and Left in a tree: open or fold the current row, or move to its child or parent.
    bool tree_key(bool right);
    // Selects a row, or makes a row that cannot be selected current, without scrolling.
    bool move_to(std::size_t);
    void set_current(Index);
    void invalidate_row(std::size_t);
    void synchronize_selection();
    void on_layout_updated() override;
    // Selects the row under a right click first; the keyboard's position is the selected row's.
    bool on_context_menu(const ContextMenuEvent&) override;
    bool on_key(const KeyEvent& e) override { return navigate(e); }
    bool on_text_input(std::wstring_view text) override { return type_ahead(text); }
    std::shared_ptr<Element> content_, rows_;   // The rows, under the overlay.
    std::shared_ptr<HostedVisual> overlay_;
    Items items_;
    std::vector<double> tops_;   // With Items::height or separators: each row's top, then the extent.
    std::vector<std::size_t> separators_;   // The separator rows, in order.
    // In a tree, each group of rows with the same parent found so far, and the group of each of
    // its rows; found when a row is first realized, forgotten when the items change.
    mutable std::vector<std::vector<std::size_t>> peerGroups_;
    mutable std::unordered_map<std::size_t, std::size_t> peerGroup_;
    Index selected_, current_;
    float rowHeight_;
    bool menu_;
    bool popupFocus_{};
    std::wstring prefix_;
    std::uint64_t lastText_{};
    RowRenderer renderer_;
    SeparatorRenderer separatorRenderer_;
    Signal<Index> changed_;
    Signal<std::size_t> activated_;
    Signal<std::size_t, bool> expand_;
};

class Picker;
// Wrap the window content once. Open popups share its surface, focus and automation tree.
class PopupHost final : public Element {
public:
    explicit PopupHost(std::shared_ptr<Element> content);
    ~PopupHost() override;
    void dismiss();
private:
    friend class Picker;
    void present(Picker&);
    void on_layout_updated() override;
    std::weak_ptr<Picker> anchor_;
    std::shared_ptr<Element> layer_;
};

class Picker : public Pressable {
public:
    bool open();
    void close();
    [[nodiscard]] bool expanded() const noexcept;
    [[nodiscard]] List::Index selected() const noexcept { return selected_; }
    [[nodiscard]] std::wstring_view selected_text() const noexcept { return selectedText_; }
    void set_selected(List::Index);
    void set_items(Items);
    [[nodiscard]] const List& popup() const noexcept { return *list_; }
    void set_row_renderer(List::RowRenderer renderer) { list_->set_row_renderer(std::move(renderer)); }
    void set_popup_renderer(Renderer renderer) { list_->set_renderer(std::move(renderer)); }
    void set_popup_tokens(std::optional<StyleTokens> tokens) { list_->set_tokens(tokens); }
    // A dropdown's value changed, or a menu's row was chosen, the same row again included.
    Connection on_selection(std::function<void(List::Index)> f) { return changed_.connect(std::move(f)); }
protected:
    Picker(std::wstring name, Items, bool menu, float rowHeight, float maxPopupHeight);
private:
    friend class PopupHost;
    PopupHost* host() const noexcept;
    void set_expanded(bool);
    // Takes a row as the value and raises on_selection, when the row changes or `always`.
    void commit(List::Index, bool always = false);
    void activate() override;
    bool on_key(const KeyEvent&) override;
    bool on_text_input(std::wstring_view) override;
    void on_focus_changed(bool) override;
    void on_detaching() noexcept override;
    std::shared_ptr<List> list_;
    List::Index selected_;
    std::wstring selectedText_;
    float maxHeight_;
    bool menu_, preview_{};
    Signal<List::Index> changed_;
    Connection selection_, activation_;
};
class Dropdown final : public Picker {
public:
    Dropdown(std::wstring name, Items items, float rowHeight = 32, float maxPopupHeight = 192)
        : Picker(std::move(name), std::move(items), false, rowHeight, maxPopupHeight) {}
};
class Menu final : public Picker {
public:
    Menu(std::wstring name, Items items, float rowHeight = 32, float maxPopupHeight = 192)
        : Picker(std::move(name), std::move(items), true, rowHeight, maxPopupHeight) {}
};

} // namespace composia::controls
