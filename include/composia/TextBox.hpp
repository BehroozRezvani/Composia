#pragma once

#include <composia/Element.hpp>
#include <composia/Native.hpp>
#include <composia/Signal.hpp>
#include <richedit.h>
#include <textserv.h>
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
    [[nodiscard]] bool modified() const; // Rich Edit's modification flag, available during input dispatch.
    void mark_saved();
    [[nodiscard]] Selection selection() const;
    void select(long start, long end); // (0, -1) selects all.
    void replace_selection(std::wstring_view);
    bool undo();
    bool redo();
    void copy();
    void cut();
    void paste();
    void set_read_only(bool);
    // Whether undo() or redo() has something to do now.
    [[nodiscard]] bool can_undo() const;
    [[nodiscard]] bool can_redo() const;
    // Changes the font, the colors and the padding (the name, multiline and read-only state stay
    // as they are) and sizes the element for the new font. Throws std::invalid_argument for an
    // invalid font or padding.
    void set_style(const Options& style);
    // A message straight to Rich Edit's text services, for what this class does not cover, such
    // as EM_GETLINECOUNT or EM_SETLIMITTEXT. TextBox keeps the text mode, the event mask and the
    // read-only state itself: change those through it, or it loses track of them.
    LRESULT send(UINT message, WPARAM wparam = 0, LPARAM lparam = 0);
    // Rich Edit's text services, under the same terms as send().
    [[nodiscard]] ITextServices2* text_services() const noexcept;
    [[nodiscard]] const Options& options() const noexcept { return options_; }
    void set_painter(Painter);
    void draw_text(ScopedSurfaceDraw&); // Includes native selection and IME decorations.
    void draw_caret(ScopedSurfaceDraw&);
    [[nodiscard]] bool caret_visible() const noexcept;
    [[nodiscard]] bool pointer_pressed() const noexcept;
    [[nodiscard]] layout::Rect caret_bounds() const noexcept;
    // Visible selection rectangles in local DIPs, including wrapped and bidirectional runs.
    [[nodiscard]] std::vector<layout::Rect> selection_rectangles() const;
    Connection on_text_changed(std::function<void()> f) { return changed_.connect(std::move(f)); }
    // Raised when the editor takes or loses the keyboard: once per change, after Rich Edit knows.
    Connection on_focus(std::function<void(bool)> f) { return focusChanged_.connect(std::move(f)); }

private:
    friend class ElementWindow;
    friend struct ElementAutomation;
    struct Host;
    std::optional<LRESULT> window_message(UINT, WPARAM, LPARAM);
    void paint(ScopedSurfaceDraw&) override;
    bool on_pointer(const PointerEvent&) override;
    bool on_key(const KeyEvent&) override;
    void on_focus_changed(bool) override;
    void on_hover_changed(bool) override { if (painter_) { invalidate(); } }
    void on_detaching() noexcept override;
    Options options_;
    Painter painter_;
    Signal<> changed_;
    Signal<bool> focusChanged_;
    wil::com_ptr<Host> host_;
};

}
