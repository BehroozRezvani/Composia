#pragma once
#include <composia/ElementAccessibility.hpp>
#include <composia/TextBox.hpp>
#include <cstdint>

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
struct StyleTokens {
    std::uint32_t foreground{0x202020}, background{0xffffff}, accent{0x0067c0};
    float padding{6}, radius{4};
};

using TextBox = composia::TextBox;
[[nodiscard]] State state_of(const TextBox&);

class Control : public Element {
public:
    using Renderer = std::function<void(ScopedSurfaceDraw&, const Control&, State, const StyleTokens&)>;
    void set_renderer(Renderer);
    void set_tokens(StyleTokens);
    [[nodiscard]] const StyleTokens& tokens() const noexcept { return tokens_; }
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
    StyleTokens tokens_;
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

class ScrollView : public Control {
public:
    explicit ScrollView(std::wstring name, layout::Axis = layout::Axis::vertical, float viewportLength = 160);
    void set_content(std::shared_ptr<Element>);
protected:
    bool on_key(const KeyEvent&) override;
private:
    layout::Axis axis_;
};

} // namespace composia::controls
