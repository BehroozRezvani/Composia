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
#include <cwctype>
#include <optional>
#include <string>
#include <string_view>

using namespace composia;

namespace {

constexpr float kThumbRadius = 9.0f;
constexpr float kHitRadius = 12.0f;

IDWriteFactory7* text_factory(Application& app) { return app.graphics().text_factory().get(); }

// The track spans x 120 to width - 32 at y 400 and is 4 DIPs tall.
layout::Rect track_rect(float width) { return {120.0f, 400.0f, std::max(1.0f, width - 152.0f), 4.0f}; }

layout::Point thumb_center(layout::Rect track, float value) {
    return {track.x + value * track.width, track.y + track.height * 0.5f};
}

bool on_slider(layout::Point p, layout::Rect track, float value) {
    const auto center = thumb_center(track, value);
    const float dx = p.x - center.x;
    const float dy = p.y - center.y;
    return dx * dx + dy * dy <= kHitRadius * kHitRadius
        || layout::Rect{track.x, track.y - 8.0f, track.width, 20.0f}.contains(p.x, p.y);
}

}

class InputsWindow final : public Window {
public:
    explicit InputsWindow(Application& app)
        : Window(app, L"Composia inputs", 960, 600),
          target_(app.compositor(), app.graphics(), hwnd()),
          title_(text_factory(app), L"Inputs", 28.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD),
          subtitle_(text_factory(app),
              L"Native EDIT controls give text input with IME, selection, clipboard, and accessibility. "
              L"The slider is painted into this window and uses its pointer capture.", 14.0f),
          nameLabel_(text_factory(app), L"Name", 14.0f),
          notesLabel_(text_factory(app), L"Notes", 14.0f),
          volume_(text_factory(app), L"Volume 42%", 15.0f),
          statusLayout_(text_factory(app), L"Ready.", 16.0f),
          name_(*this, L"EDIT", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, L""),
          notes_(*this, L"EDIT", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN, L""),
          loud_(*this, L"BUTTON", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, L"Shout"),
          greet_(*this, L"Say hello") {
        name_.send(EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Your name"));
        name_.set_colors(RGB(234, 242, 244), RGB(14, 22, 31));
        notes_.set_colors(RGB(234, 242, 244), RGB(14, 22, 31));
        loud_.set_colors(RGB(234, 242, 244), RGB(16, 25, 35));

        loudClick_ = loud_.on_command([this](UINT code) {
            if (code == BN_CLICKED) {
                loudOn_ = loud_.send(BM_GETCHECK) == BST_CHECKED;
                invalidate();
            }
        });
        greetClick_ = greet_.on_click([this] {
            const std::wstring name = name_.text();
            std::wstring greeting = name.empty() ? std::wstring{L"Hello, stranger!"} : L"Hello, " + name + L"!";
            if (loudOn_) {
                for (auto& ch : greeting) { ch = static_cast<wchar_t>(std::towupper(ch)); }
            }
            status_ = greeting;
            invalidate();
        });
    }

protected:
    void on_resize() override {
        arrange();
        invalidate();
    }

    void on_paint() override {
        arrange();
        target_.render(*this, [this](ScopedSurfaceDraw& draw, numerics::float2 size) { draw_canvas(draw, size); });
    }

    void on_hover(bool) override { invalidate(); }
    void on_focus(bool) override { invalidate(); }

    void on_capture_lost() override {
        dragging_ = false;
        invalidate();
    }

    std::optional<LRESULT> on_message(UINT message, WPARAM wparam, LPARAM lparam) override {
        switch (message) {
        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
            const auto windowDpi = hwnd() ? dpi() : GetDpiForSystem();
            info->ptMinTrackSize = {MulDiv(640, windowDpi, 96), MulDiv(480, windowDpi, 96)};
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
        case WM_LBUTTONDOWN: {
            const auto point = pointer_position(lparam);
            if (!on_slider(point, track_rect(client_bounds().width), value_)) { break; }
            focus();
            capture_pointer();
            dragging_ = true;
            set_value_from(point.x);
            invalidate();
            return 0;
        }
        case WM_MOUSEMOVE:
            lastPointer_ = pointer_position(lparam);
            if (dragging_) { set_value_from(lastPointer_.x); }
            invalidate();
            break;
        case WM_LBUTTONUP:
            if (dragging_) {
                release_pointer();
                dragging_ = false;
                invalidate();
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (wparam == VK_LEFT || wparam == VK_RIGHT) {
                const float step = GetKeyState(VK_SHIFT) < 0 ? 0.2f : 0.05f;
                value_ = std::clamp(value_ + (wparam == VK_RIGHT ? step : -step), 0.0f, 1.0f);
                invalidate();
                return 0;
            }
            break;
        default:
            break;
        }
        return std::nullopt;
    }

private:
    void arrange() {
        const float width = client_bounds().width;
        const float controlWidth = std::max(200.0f, width - 152.0f);
        name_.set_bounds({120.0f, 96.0f, controlWidth, 32.0f});
        notes_.set_bounds({120.0f, 144.0f, controlWidth, 160.0f});
        loud_.set_bounds({120.0f, 320.0f, 160.0f, 24.0f});
        greet_.set_bounds({300.0f, 320.0f, 140.0f, 36.0f});
        volume_ = TextLayout{text_factory(application()), L"Volume " + std::to_wstring(percent()) + L"%", 15.0f};
        statusLayout_ = TextLayout{text_factory(application()), status_, 16.0f};
    }

    int percent() const { return static_cast<int>(std::lround(value_ * 100.0f)); }

    void set_value_from(float x) {
        const auto track = track_rect(client_bounds().width);
        value_ = std::clamp((x - track.x) / track.width, 0.0f, 1.0f);
    }

    void draw_canvas(ScopedSurfaceDraw& draw, numerics::float2 size) {
        const auto dc = draw.context().get();
        dc->Clear(D2D1::ColorF(0x101923));
        const float inner = std::max(1.0f, size.x - 64.0f);
        const auto text = [&](TextLayout& label, float x, float y, float width, float height, UINT32 color) {
            label.resize(width, height);
            dc->DrawTextLayout({x, y}, label.layout().get(), draw.solid_brush(color));
        };
        text(title_, 32.0f, 24.0f, inner, 40.0f, 0xEAF2F4);
        text(subtitle_, 32.0f, 60.0f, inner, 36.0f, 0x93A9B5);
        text(nameLabel_, 32.0f, 102.0f, 80.0f, 20.0f, 0xEAF2F4);
        text(notesLabel_, 32.0f, 144.0f, 80.0f, 20.0f, 0xEAF2F4);
        text(volume_, 120.0f, 424.0f, std::max(1.0f, size.x - 152.0f), 22.0f, 0xEAF2F4);
        text(statusLayout_, 32.0f, 460.0f, inner, 28.0f, 0x6FE6C8);

        // The slider has no child HWND; it is drawn here and reads pointer input in on_message.
        const auto track = track_rect(size.x);
        const auto thumb = thumb_center(track, value_);
        const bool highlight = (hovered() && on_slider(lastPointer_, track, value_)) || dragging_;
        dc->FillRoundedRectangle(D2D1::RoundedRect({track.x, track.y, track.x + track.width, track.y + track.height}, 2.0f, 2.0f),
            draw.solid_brush(0x2A3946));
        dc->FillRoundedRectangle(D2D1::RoundedRect({track.x, track.y, thumb.x, track.y + track.height}, 2.0f, 2.0f),
            draw.solid_brush(0x6FE6C8));
        dc->FillEllipse(D2D1::Ellipse({thumb.x, thumb.y}, kThumbRadius, kThumbRadius),
            draw.solid_brush(highlight ? 0xA2F5DF : 0x6FE6C8));
        if (focused()) {
            const layout::Rect area{track.x - 12.0f, 384.0f, track.width + 24.0f, 36.0f};
            dc->DrawRoundedRectangle(D2D1::RoundedRect({area.x, area.y, area.x + area.width, area.y + area.height}, 6.0f, 6.0f),
                draw.solid_brush(0xFFFFFF), 1.5f);
        }
    }

    CompositionWindowTarget target_;
    TextLayout title_;
    TextLayout subtitle_;
    TextLayout nameLabel_;
    TextLayout notesLabel_;
    TextLayout volume_;
    TextLayout statusLayout_;
    NativeControl name_;
    NativeControl notes_;
    NativeControl loud_;
    Button greet_;
    Connection loudClick_;
    Connection greetClick_;
    bool loudOn_{};
    float value_{0.42f};
    bool dragging_{};
    layout::Point lastPointer_{};
    std::wstring status_{L"Ready."};
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR commandLine, int showCommand) {
    const std::wstring_view arguments{commandLine};
    const bool warp = arguments.find(L"--warp") != std::wstring_view::npos;
    try {
        Application app{warp};
        int result{};
        {
            InputsWindow window{app};
            window.show(showCommand);
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
