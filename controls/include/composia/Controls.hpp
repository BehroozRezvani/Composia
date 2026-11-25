#pragma once
#include <composia/Appearance.hpp>
#include <composia/ElementAccessibility.hpp>
#include <composia/TextBox.hpp>
#include <cstdint>
#include <optional>

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

struct State { bool hovered{}, pressed{}, focused{}, disabled{}, selected{}; };
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
protected:
    Control(std::wstring name, long role, ElementLayout = {});
    void paint(ScopedSurfaceDraw&) override;
    void on_hover_changed(bool) override { invalidate(); }
    void on_capture_lost() noexcept override;
    void on_focus_changed(bool) override;
    void set_pressed(bool);
private:
    bool pressed_{};
    Renderer renderer_;
    std::optional<StyleTokens> tokens_;
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
    static Items strings(std::vector<std::wstring>);
};

class List final : public ScrollView {
public:
    using Index = std::optional<std::size_t>;
    using RowRenderer = std::function<void(ScopedSurfaceDraw&, std::size_t, std::wstring_view,
                                            layout::Size, State, const StyleTokens&)>;
    List(std::wstring name, Items, float rowHeight = 32, float viewportHeight = 160);
    ~List() override;
    void set_items(Items);
    void set_row_renderer(RowRenderer);
    [[nodiscard]] const Items& items() const noexcept { return items_; }
    [[nodiscard]] Index selected() const noexcept { return selected_; }
    bool select(Index);
    void reveal(std::size_t);
    bool navigate(const KeyEvent&);
    bool type_ahead(std::wstring_view);
    bool activate_selected();
    [[nodiscard]] std::size_t realized_count() const noexcept;
    Connection on_selection(std::function<void(Index)> f) { return changed_.connect(std::move(f)); }
    Connection on_activate(std::function<void(std::size_t)> f) { return activated_.connect(std::move(f)); }
private:
    friend class Picker;
    class Row;
    List(std::wstring, Items, float, float, bool menu);
    Row& realize(std::size_t);
    void synchronize_selection();
    void on_layout_updated() override;
    bool on_key(const KeyEvent& e) override { return navigate(e); }
    bool on_text_input(std::wstring_view text) override { return type_ahead(text); }
    std::shared_ptr<Element> rows_;
    Items items_;
    Index selected_;
    float rowHeight_;
    bool menu_;
    bool popupFocus_{};
    std::wstring prefix_;
    std::uint64_t lastText_{};
    RowRenderer renderer_;
    Signal<Index> changed_;
    Signal<std::size_t> activated_;
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
    Connection on_selection(std::function<void(List::Index)> f) { return changed_.connect(std::move(f)); }
protected:
    Picker(std::wstring name, Items, bool menu, float rowHeight, float maxPopupHeight);
private:
    friend class PopupHost;
    PopupHost* host() const noexcept;
    void set_expanded(bool);
    void commit(List::Index);
    void activate() override;
    bool on_key(const KeyEvent&) override;
    bool on_text_input(std::wstring_view) override;
    void on_focus_changed(bool) override;
    void on_detaching() noexcept override;
    std::shared_ptr<List> list_;
    List::Index selected_;
    std::wstring selectedText_;
    float rowHeight_, maxHeight_;
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
