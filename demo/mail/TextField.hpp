#pragma once

#include <composia/Application.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/Signal.hpp>
#include <composia/TextLayout.hpp>
#include <optional>
#include <string>

// A composition-rendered, keyboard-editable text box hosted in a child HWND. Single-line fields
// submit on Enter; multiline fields insert a newline. Tab moves focus through the parent's controls.
class TextField final : public composia::Window {
public:
    TextField(composia::Window& parent, std::wstring_view placeholder, bool multiline = false);
    ~TextField() override;
    [[nodiscard]] const std::wstring& text() const noexcept { return text_; }
    void set_text(std::wstring value);
    void insert(std::wstring_view value);
    void set_caret(std::size_t position) { move_caret(position); }
    void focus();
    [[nodiscard]] bool focused() const noexcept { return focused_; }
    [[nodiscard]] bool multiline() const noexcept { return multiline_; }
    [[nodiscard]] std::size_t caret() const noexcept { return caret_; }
    composia::Connection on_change(std::function<void()> callback) { return changed_.connect(std::move(callback)); }
    composia::Connection on_submit(std::function<void()> callback) { return submitted_.connect(std::move(callback)); }

private:
    void on_resize() override { invalidate(); }
    void on_paint() override;
    void on_graphics_recreated() override { brush_.reset(); }
    std::optional<LRESULT> on_message(UINT, WPARAM, LPARAM) override;
    void changed();
    void move_caret(std::size_t position);
    void move_line(int direction);
    void erase(bool backward, bool word);
    void paste();
    void place_caret(float x, float y);
    [[nodiscard]] composia::TextLayout build_layout(float width, float height) const;
    [[nodiscard]] std::size_t step(std::size_t position, int direction) const noexcept;

    composia::CompositionWindowTarget target_;
    composia::composition::SpriteVisual caret_visual_{nullptr};
    wil::com_ptr<ID2D1SolidColorBrush> brush_;
    std::wstring text_, placeholder_;
    std::size_t caret_{};
    float scroll_{};
    bool multiline_{}, focused_{};
    composia::Signal<> changed_, submitted_;
};
