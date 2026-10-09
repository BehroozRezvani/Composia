#include <composia/Accessible.hpp>
#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <composia/CompositionWindowTarget.hpp>
#include <composia/NativeControl.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/Signal.hpp>
#include <composia/TextLayout.hpp>
#include <commctrl.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>

using namespace composia;

namespace {

constexpr UINT32 canvasColor = 0x101923;
constexpr UINT32 fieldColor = 0x18232E;
constexpr UINT32 textColor = 0xEAF2F4;
constexpr UINT32 mutedColor = 0x93A9B5;
constexpr UINT32 accentColor = 0x6FE6C8;
constexpr COLORREF fieldText = RGB(234, 242, 244), fieldBackground = RGB(24, 35, 46), canvasBackground = RGB(16, 25, 35);

IDWriteFactory7* text_factory(Application& app) { return app.graphics().text_factory().get(); }
std::wstring percent_text(float value) { return std::to_wstring(std::lround(value * 100.0f)) + L"%"; }

// A slider drawn into its own child window. One HWND makes it a tab stop that the dialog
// navigation reaches, gives it keyboard focus, and lets it carry a UI Automation element; the
// track, fill, and thumb inside it are drawn, not windows.
class Slider final : public Window {
public:
    Slider(Window& parent, std::wstring_view name, float value)
        : Window(parent.application(), name, 300, 40, parent.hwnd()),
          target_(application().compositor(), application().graphics(), hwnd()),
          value_(std::clamp(value, 0.0f, 1.0f)),
          accessible_(*this, {.name = std::wstring{name}, .controlType = UIA_SliderControlTypeId, .value = true,
              .setValue = [this](std::wstring text) { set_value(static_cast<float>(std::wcstol(text.c_str(), nullptr, 10)) / 100.0f); },
              .automationId = L"volume"}) {
        accessible_.set_value(percent_text(value_));
    }

    [[nodiscard]] float value() const noexcept { return value_; }
    Connection on_change(std::function<void(float)> callback) { return changed_.connect(std::move(callback)); }

    void set_value(float value) {
        value = std::clamp(value, 0.0f, 1.0f);
        if (value == value_) { return; }
        value_ = value;
        accessible_.set_value(percent_text(value_));
        invalidate();
        changed_.emit(value_);
    }

private:
    static constexpr float inset = 12.0f;

    void on_resize() override { invalidate(); }
    void on_hover(bool) override { invalidate(); }
    void on_focus(bool) override { invalidate(); }
    void on_enabled(bool) override { invalidate(); }
    void on_capture_lost() override {
        dragging_ = false;
        invalidate();
    }

    void on_paint() override {
        target_.render(*this, [this](ScopedSurfaceDraw& draw, numerics::float2 size) {
            const auto dc = draw.context().get();
            dc->Clear(D2D1::ColorF(canvasColor));
            const float middle = size.y * 0.5f, right = std::max(inset, size.x - inset);
            const float thumb = inset + value_ * (right - inset);
            const bool active = enabled();
            dc->FillRoundedRectangle(D2D1::RoundedRect({inset, middle - 2, right, middle + 2}, 2, 2), draw.solid_brush(0x2A3946));
            dc->FillRoundedRectangle(D2D1::RoundedRect({inset, middle - 2, thumb, middle + 2}, 2, 2), draw.solid_brush(active ? accentColor : 0x35434B));
            const UINT32 thumbColor = !active ? 0x35434B : (hovered() || dragging_) ? 0xA2F5DF : accentColor;
            dc->FillEllipse(D2D1::Ellipse({thumb, middle}, 9, 9), draw.solid_brush(thumbColor));
            if (focused()) {
                dc->DrawRoundedRectangle(D2D1::RoundedRect({1, 1, size.x - 1, size.y - 1}, 6, 6), draw.solid_brush(0xFFFFFF), 1.5f);
            }
        });
    }

    void set_from(LPARAM position) {
        const auto width = client_bounds().width;
        set_value((pointer_position(position).x - inset) / std::max(1.0f, width - 2 * inset));
    }

    std::optional<LRESULT> on_message(UINT message, WPARAM wparam, LPARAM lparam) override {
        switch (message) {
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;  // Arrow keys adjust the value instead of moving the focus.
        case WM_LBUTTONDOWN:
            if (!enabled()) { return 0; }
            focus();
            capture_pointer();
            dragging_ = true;
            set_from(lparam);
            invalidate();
            return 0;
        case WM_MOUSEMOVE:
            if (dragging_) { set_from(lparam); }
            return 0;
        case WM_LBUTTONUP:
            if (dragging_) {
                dragging_ = false;
                release_pointer();
                invalidate();
            }
            return 0;
        case WM_KEYDOWN: {
            const float step = GetKeyState(VK_SHIFT) < 0 ? 0.2f : 0.05f;
            switch (wparam) {
            case VK_LEFT: case VK_DOWN: set_value(value_ - step); return 0;
            case VK_RIGHT: case VK_UP: set_value(value_ + step); return 0;
            case VK_PRIOR: set_value(value_ + 0.2f); return 0;
            case VK_NEXT: set_value(value_ - 0.2f); return 0;
            case VK_HOME: set_value(0); return 0;
            case VK_END: set_value(1); return 0;
            default: break;
            }
            break;
        }
        default:
            break;
        }
        return std::nullopt;
    }

    CompositionWindowTarget target_;
    float value_{};
    bool dragging_{};
    Signal<float> changed_;
    Accessible accessible_;
};

}

// Native EDIT controls and a check box for text and choices, a framework Button, and a painted
// slider, over text drawn into the window's composition canvas. Everything is event driven: a
// change repaints only the area it affects, and layout runs when the size or DPI changes.
class InputsWindow final : public Window {
public:
    explicit InputsWindow(Application& app)
        : Window(app, L"Composia inputs", 960, 600),
          target_(app.compositor(), app.graphics(), hwnd()),
          title_(text_factory(app), L"Inputs", 28.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD),
          subtitle_(text_factory(app),
              L"The text fields and the check box are native controls, so IME, selection, the clipboard, and accessibility come "
              L"from Windows. The slider is drawn into one child window that is a tab stop with its own UI Automation element.", 14.0f),
          nameLabel_(text_factory(app), L"Name", 14.0f),
          notesLabel_(text_factory(app), L"Notes", 14.0f),
          volumeLabel_(text_factory(app), L"Volume", 14.0f),
          volumeValue_(text_factory(app), percent_text(0.42f), 14.0f),
          hint_(text_factory(app), L"", 13.0f),
          status_(text_factory(app), L"Ready.", 16.0f),
          name_(*this, L"EDIT", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL),
          notes_(*this, L"EDIT", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN),
          loud_(*this, L"BUTTON", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, L"Shout"),
          greet_(*this, L"Say hello"),
          volume_(*this, L"Volume", 0.42f) {
        name_.send(EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Your name"));
        name_.set_colors(fieldText, fieldBackground);
        notes_.set_colors(fieldText, fieldBackground);
        loud_.set_colors(fieldText, canvasBackground);  // Check boxes take the classic look to honor colors.
        greetClick_ = greet_.on_click([this] { greet(); });
        volumeChange_ = volume_.on_change([this](float value) {
            volumeValue_ = TextLayout{text_factory(application()), percent_text(value), 14.0f};
            invalidate(volumeValueArea_);
        });
        // The parent watches the slider's focus through its public signal.
        volumeFocus_ = volume_.on_focus_changed([this](bool focused) {
            hint_ = TextLayout{text_factory(application()),
                focused ? L"Arrow keys adjust the volume; Shift, Page Up, and Page Down take bigger steps; Home and End go to the ends." : L"", 13.0f};
            invalidate(hintArea_);
        });
        arrange();
    }

    void start(int showCommand) {
        show(showCommand);
        name_.focus();
    }

protected:
    void on_resize() override {
        arrange();
        invalidate();
    }

    void on_paint() override {
        target_.render(*this, [this](ScopedSurfaceDraw& draw, numerics::float2) { draw_canvas(draw); });
    }

    std::optional<LRESULT> on_message(UINT message, WPARAM, LPARAM lparam) override {
        if (message == WM_GETMINMAXINFO) {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            const auto windowDpi = hwnd() ? dpi() : GetDpiForSystem();
            info->ptMinTrackSize = {MulDiv(640, windowDpi, 96), MulDiv(560, windowDpi, 96)};
            return 0;
        }
        return std::nullopt;
    }

private:
    // Positions the child windows and records the areas that change on their own. Runs on resize,
    // which also follows a DPI change, never while painting.
    void arrange() {
        const float width = client_bounds().width;
        const float fields = std::max(200.0f, width - 152.0f);
        nameArea_ = {120, 116, fields, 28};
        notesArea_ = {120, 160, fields, 132};
        name_.set_bounds(nameArea_);
        notes_.set_bounds(notesArea_);
        loud_.set_bounds({120, 310, 140, 24});
        greet_.set_bounds({280, 304, 150, 36});
        volume_.set_bounds({112, 360, fields + 8, 40});
        volumeValueArea_ = {120, 404, 120, 20};
        hintArea_ = {120, 426, fields, 20};
        statusArea_ = {32, 470, std::max(1.0f, width - 64), 28};
    }

    void greet() {
        std::wstring greeting = name_.text().empty() ? std::wstring{L"Hello, stranger!"} : L"Hello, " + name_.text() + L"!";
        if (loud_.send(BM_GETCHECK) == BST_CHECKED) {
            for (auto& ch : greeting) { ch = static_cast<wchar_t>(std::towupper(ch)); }
        }
        status_ = TextLayout{text_factory(application()), greeting, 16.0f};
        invalidate(statusArea_);
    }

    void draw_canvas(ScopedSurfaceDraw& draw) {
        const auto dc = draw.context().get();
        dc->Clear(D2D1::ColorF(canvasColor));
        const float width = client_bounds().width;
        const auto text = [&](TextLayout& label, layout::Rect area, UINT32 color) {
            label.resize(std::max(1.0f, area.width), std::max(1.0f, area.height));
            dc->DrawTextLayout({area.x, area.y}, label.layout().get(), draw.solid_brush(color));
        };
        text(title_, {32, 24, width - 64, 40}, textColor);
        text(subtitle_, {32, 64, width - 64, 40}, mutedColor);
        // Field backgrounds a little larger than the EDIT controls, which sit on top of the canvas.
        for (const auto area : {nameArea_, notesArea_}) {
            const auto frame = D2D1::RoundedRect({area.x - 8, area.y - 4, area.x + area.width + 4, area.y + area.height + 4}, 6, 6);
            dc->FillRoundedRectangle(frame, draw.solid_brush(fieldColor));
            dc->DrawRoundedRectangle(frame, draw.solid_brush(0x2A3946), 1.0f);
        }
        text(nameLabel_, {32, nameArea_.y + 4, 80, 20}, textColor);
        text(notesLabel_, {32, notesArea_.y + 4, 80, 20}, textColor);
        text(volumeLabel_, {32, 370, 80, 20}, textColor);
        text(volumeValue_, volumeValueArea_, textColor);
        text(hint_, hintArea_, mutedColor);
        text(status_, statusArea_, accentColor);
    }

    CompositionWindowTarget target_;
    TextLayout title_, subtitle_, nameLabel_, notesLabel_, volumeLabel_, volumeValue_, hint_, status_;
    NativeControl name_, notes_, loud_;
    Button greet_;
    Slider volume_;
    Connection greetClick_, volumeChange_, volumeFocus_;
    layout::Rect nameArea_{}, notesArea_{}, volumeValueArea_{}, hintArea_{}, statusArea_{};
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int showCommand) {
    const std::wstring_view arguments{commandLine};
    const bool warp = arguments.find(L"--warp") != std::wstring_view::npos;
    try {
        Application app{warp};
        int result{};
        {
            InputsWindow window{app};
            window.start(showCommand);
            result = app.run();
        }
        app.close();
        return result;
    } catch (const winrt::hresult_error&) {
    } catch (const std::exception&) {
    } catch (...) {
    }
    MessageBoxW(nullptr, L"Composia could not continue.", L"Composia inputs", MB_OK | MB_ICONERROR);
    return 1;
}
