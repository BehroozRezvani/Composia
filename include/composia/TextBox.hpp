#pragma once

#include <composia/Element.hpp>
#include <composia/Native.hpp>
#include <composia/Signal.hpp>
#include <string>
#include <string_view>

namespace composia {

// A plain-text editor painted into its ElementWindow's composition surface. Windows Rich Edit
// owns the document, shaping, IME composition, selection, wrapping, scrolling and edit history.
// Construct and use it on the Application thread. Positions count UTF-16 code units; Rich Edit
// uses one carriage return per paragraph in its document and selection coordinates.
class TextBox final : public Element {
public:
    struct Options {
        std::wstring name;
        bool multiline{};
        bool readOnly{};
        std::wstring fontFamily{L"Segoe UI"};
        float fontSize{16}; // DIPs
        float padding{6};   // DIPs on each edge
        COLORREF foreground{GetSysColor(COLOR_WINDOWTEXT)};
        COLORREF background{GetSysColor(COLOR_WINDOW)};
        COLORREF selectionColor{GetSysColor(COLOR_HIGHLIGHT)};
        COLORREF selectedTextColor{GetSysColor(COLOR_HIGHLIGHTTEXT)};
    };
    struct Selection {
        long start{}, end{};
    };
    // Draw arbitrary chrome and call draw_text and draw_caret at the element's local origin.
    using Painter = std::function<void(ScopedSurfaceDraw&, TextBox&)>;

    explicit TextBox(Options, std::wstring_view initialText = {}, Painter = {});
    ~TextBox() override;
    [[nodiscard]] std::wstring text() const;
    void set_text(std::wstring_view); // Replaces the document and resets undo history.
    [[nodiscard]] Selection selection() const;
    void select(long start, long end); // (0, -1) selects all.
    void replace_selection(std::wstring_view);
    bool undo();
    bool redo();
    void copy();
    void cut();
    void paste();
    void set_read_only(bool);
    [[nodiscard]] const Options& options() const noexcept { return options_; }
    void set_painter(Painter);
    void draw_text(ScopedSurfaceDraw&); // Includes native selection and IME decorations.
    void draw_caret(ScopedSurfaceDraw&);
    [[nodiscard]] bool caret_visible() const noexcept;
    [[nodiscard]] layout::Rect caret_bounds() const noexcept;
    // Visible selection rectangles in local DIPs, including wrapped and bidirectional runs.
    [[nodiscard]] std::vector<layout::Rect> selection_rectangles() const;
    Connection on_text_changed(std::function<void()> f) { return changed_.connect(std::move(f)); }

private:
    friend class ElementWindow;
    friend struct TextBoxAutomation;
    struct Host;
    std::optional<LRESULT> window_message(UINT, WPARAM, LPARAM);
    void paint(ScopedSurfaceDraw&) override;
    bool on_pointer(const PointerEvent&) override;
    bool on_key(const KeyEvent&) override;
    void on_focus_changed(bool) override;
    void on_detaching() noexcept override;
    Options options_;
    Painter painter_;
    Signal<> changed_;
    wil::com_ptr<Host> host_;
};

}
