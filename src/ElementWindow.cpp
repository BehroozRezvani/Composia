#include <composia/ElementWindow.hpp>
#include <composia/Application.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include <composia/HostedWindow.hpp>
#include <composia/TextBox.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <commctrl.h>
#include <uxtheme.h>
#include "ElementAutomation.hpp"

namespace composia {
namespace {
UINT update_message() {
    static const auto id = RegisterWindowMessageW(L"Composia.ElementWindow.Update");
    THROW_LAST_ERROR_IF(!id);
    return id;
}

bool key_down(int key) { return (GetKeyState(key) & 0x8000) != 0; }

// The element whose tooltip applies: the nearest one, from the element up, that has one.
Element* tooltip_owner(Element* element) {
    while (element && element->tooltip().empty()) { element = element->parent(); }
    return element;
}
}

ElementWindow::ElementWindow(Application& app, std::wstring_view title, int widthDip, int heightDip,
    std::shared_ptr<Element> root, HWND parent)
    : ElementWindow(app, title, widthDip, heightDip, Content::composition, std::move(root), parent) {}

ElementWindow::ElementWindow(Application& app, std::wstring_view title, int widthDip, int heightDip,
    Content content, std::shared_ptr<Element> root, HWND parent)
    : Window(app, title, widthDip, heightDip, parent, content == Content::composition ? WS_EX_NOREDIRECTIONBITMAP : 0, true),
      tree_(std::move(root), [this] {
          if (hwnd()) { THROW_IF_WIN32_BOOL_FALSE(PostMessageW(hwnd(), update_message(), 0, 0)); }
      }),
      target_(*this, content == Content::composition && (!parent || CompositionWindowTarget::hosts_children(parent))
          ? CompositionWindowTarget::Hosting::shared : CompositionWindowTarget::Hosting::standalone) {
    tree_.window_ = this;
    destroying_ = on_destroy([this] {
        hide_tooltip();
        tooltip_.reset();
        ElementAutomation::disconnect(*this);
        tree_.release_window();
    });
    on_resize();
}

ElementWindow::~ElementWindow() { ElementAutomation::disconnect(*this); tree_.release_window(); }

void ElementWindow::follow_tooltip(Element* source, bool keyboard) {
    const auto current = tipSource_.lock();
    if (current.get() == source) { return; }
    hide_tooltip();
    tipSource_ = tree_.reference(source);
    tipKeyboard_ = keyboard;
    if (source && hwnd()) { tipDelay_.start(std::chrono::milliseconds{GetDoubleClickTime()}); }
}

void ElementWindow::set_dark_tooltips(bool dark) {
    darkTooltips_ = dark;
    if (tooltip_) { SetWindowTheme(tooltip_.get(), dark ? L"DarkMode_Explorer" : nullptr, nullptr); }
}

void ElementWindow::show_tooltip() {
    tipDelay_.stop();
    const auto source = tipSource_.lock();
    if (!source || source->tooltip().empty() || !hwnd() || !IsWindowVisible(hwnd())) { return; }
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_TRACK | TTF_ABSOLUTE | TTF_IDISHWND;
    tool.hwnd = hwnd();
    tool.uId = reinterpret_cast<UINT_PTR>(hwnd());
    tipText_ = source->tooltip();
    tool.lpszText = tipText_.data();
    if (!tooltip_) {
        tooltip_.reset(CreateWindowExW(WS_EX_TOPMOST | WS_EX_NOACTIVATE, TOOLTIPS_CLASSW, nullptr,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
            hwnd(), nullptr, GetModuleHandleW(nullptr), nullptr));
        THROW_LAST_ERROR_IF_NULL(tooltip_.get());
        if (darkTooltips_) { SetWindowTheme(tooltip_.get(), L"DarkMode_Explorer", nullptr); }
        THROW_LAST_ERROR_IF(!SendMessageW(tooltip_.get(), TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool)));
    }
    SendMessageW(tooltip_.get(), TTM_SETMAXTIPWIDTH, 0, MulDiv(400, static_cast<int>(dpi()), 96));
    SendMessageW(tooltip_.get(), TTM_UPDATETIPTEXTW, 0, reinterpret_cast<LPARAM>(&tool));
    // Below the element for keyboard focus, below the pointer's hot spot otherwise.
    const auto box = source->viewport_bounds(true);
    const auto anchor = tipKeyboard_ ? layout::Point{box.x, box.y + box.height}
                                     : layout::Point{lastPointer_.x, lastPointer_.y + 20};
    POINT at{static_cast<LONG>(anchor.x * scale()), static_cast<LONG>(anchor.y * scale())};
    ClientToScreen(hwnd(), &at);
    SendMessageW(tooltip_.get(), TTM_TRACKPOSITION, 0, MAKELPARAM(at.x, at.y));
    SendMessageW(tooltip_.get(), TTM_TRACKACTIVATE, TRUE, reinterpret_cast<LPARAM>(&tool));
    tipShown_ = true;
}

void ElementWindow::hide_tooltip() noexcept {
    tipDelay_.stop();
    if (tooltip_ && tipShown_) {
        TOOLINFOW tool{};
        tool.cbSize = sizeof(tool);
        tool.hwnd = hwnd();
        tool.uId = reinterpret_cast<UINT_PTR>(hwnd());
        SendMessageW(tooltip_.get(), TTM_TRACKACTIVATE, FALSE, reinterpret_cast<LPARAM>(&tool));
    }
    tipShown_ = false;
}

void ElementWindow::refresh_tooltip() {
    const auto source = tipSource_.lock();
    if (!source) { return; }
    // The element was removed, lost the focus or the pointer, or its text changed.
    auto* expected = tooltip_owner(tipKeyboard_ ? tree_.focused() : tree_.hovered_);
    if (expected != source.get()) { follow_tooltip(expected, tipKeyboard_); return; }
    if (tipShown_ && source->tooltip() != tipText_) {
        hide_tooltip();
        show_tooltip();
    }
}

bool ElementWindow::focus_element(Element& element) {
    if (!hwnd() || !enabled() || !tree_.focus(&element)) { return false; }
    if (auto* hosted = dynamic_cast<HostedWindow*>(&element)) {
        tree_.place_windows();
        return hosted->take_focus();
    }
    focus();
    return focused() && tree_.focused() == &element;
}

HWND ElementWindow::outside_tab_stop(bool backwards) const {
    const auto root = GetAncestor(hwnd(), GA_ROOT);
    const auto hosted = [&](HWND window) {
        return std::ranges::any_of(tree_.hosted_, [&](const HostedWindow* h) { return h->hwnd() == window; });
    };
    auto other = GetNextDlgTabItem(root, hwnd() == root ? nullptr : hwnd(), backwards);
    // Hosted windows take their turn in the tree's own order. The dialog manager wraps around,
    // so a cycle of hosted windows ends where it started.
    for (const auto first = other; other && other != hwnd() && hosted(other);) {
        other = GetNextDlgTabItem(root, other, backwards);
        if (other == first) { return nullptr; }
    }
    return other == hwnd() ? nullptr : other;
}

void ElementWindow::hand_focus_to_hosted_window() {
    if (auto* hosted = dynamic_cast<HostedWindow*>(tree_.focused()); hosted && focused()) { hosted->take_focus(); }
}

composition::SpriteVisual ElementWindow::snapshot(layout::Rect area) {
    if (!std::isfinite(area.x) || !std::isfinite(area.y) || !std::isfinite(area.width) || !std::isfinite(area.height) ||
        area.width <= 0 || area.height <= 0) {
        throw std::invalid_argument("Expected an area with a size");
    }
    auto& app = application();
    const double scale = this->scale();
    const winrt::Windows::Foundation::Size pixels{static_cast<float>(std::ceil(area.width * scale)),
                                                  static_cast<float>(std::ceil(area.height * scale))};
    composition::CompositionDrawingSurface surface{nullptr};
    app.render([&] {
        surface = app.graphics().composition_device().CreateDrawingSurface(pixels,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
        ScopedSurfaceDraw draw{surface, app.graphics(), dpi()};
        tree_.paint(draw, area);
        draw.finish();
    });
    auto sprite = app.compositor().CreateSpriteVisual();
    sprite.Brush(app.compositor().CreateSurfaceBrush(surface));
    sprite.Size({area.width, area.height});
    return sprite;
}

void ElementWindow::on_resize() {
    const auto b = client_bounds();
    tree_.set_viewport({b.width, b.height});
    tree_.place_windows();
    invalidate();
}

void ElementWindow::on_paint() {
    target_.render(*this, [&](ScopedSurfaceDraw& draw, numerics::float2 size) {
        tree_.set_viewport({size.x, size.y});
        tree_.paint(draw);
    });
}

void ElementWindow::on_focus(bool value) {
    if (tree_.releasing_) { return; }
    if (!value) {
        if (tipKeyboard_) { follow_tooltip(nullptr, false); }
        tree_.release_capture();
        previousFocus_ = tree_.reference(tree_.focused());
        tree_.focus(nullptr);
        if (auto previous = previousFocus_.lock(); previous && !focused()) {
            ElementAutomation::focus_changed(*previous, false);
        }
    }
    else if (auto current = tree_.reference(tree_.focused())) {
        // A hosted window takes the focus in the next update, outside this focus change.
        if (dynamic_cast<HostedWindow*>(current.get())) { PostMessageW(hwnd(), update_message(), 0, 0); return; }
        if (auto* editor = dynamic_cast<TextBox*>(current.get())) { editor->on_focus_changed(true); }
        if (current->focused()) { ElementAutomation::focus_changed(*current, true); }
    }
    else if (!pointerFocus_) {
        auto previous = previousFocus_.lock();
        if (!key_down(VK_TAB) && previous && tree_.focus(previous.get())) { return; }
        tree_.move_focus(key_down(VK_SHIFT));
    }
}

std::optional<LRESULT> ElementWindow::on_message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (automation_ && (message == WM_WINDOWPOSCHANGED || message == WM_SHOWWINDOW)) {
        automation_->geometryDirty = true;
        PostMessageW(hwnd(), update_message(), 0, 0);
    }
    if (message == WM_GETOBJECT && static_cast<LONG>(lparam) == UiaRootObjectId) { ElementAutomation::attach(*this); }
    if (message == update_message()) {
        if (const auto region = tree_.take_dirty_bounds()) { invalidate(*region); }
        tree_.place_windows();
        hand_focus_to_hosted_window();
        refresh_tooltip();
        ElementAutomation::refresh(*this);
        return 0;
    }
    if (message == WM_CONTEXTMENU && reinterpret_cast<HWND>(wparam) == hwnd()) {
        if (!enabled()) { return std::nullopt; }
        hide_tooltip();
        // Shift+F10 and the menu key send (-1, -1) instead of a screen position.
        const bool keyboard = lparam == static_cast<LPARAM>(-1) || (LOWORD(lparam) == 0xFFFF && HIWORD(lparam) == 0xFFFF);
        const auto handled = keyboard ? tree_.context_menu(std::nullopt)
                                      : tree_.context_menu(pointer_position_from_screen(lparam));
        return handled ? std::optional<LRESULT>{0} : std::nullopt;
    }
    using Kind = PointerEvent::Kind;
    using Button = PointerEvent::Button;
    PointerEvent event{};
    if (auto* editor = dynamic_cast<TextBox*>(tree_.focused()); editor && enabled()) {
        if (auto result = editor->window_message(message, wparam, lparam)) { return result; }
    }
    event.modifiers = LOWORD(wparam);
    event.doubleClick = message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDBLCLK ||
        message == WM_MBUTTONDBLCLK || message == WM_XBUTTONDBLCLK;
    switch (message) {
    case WM_SETCURSOR: {
        if (reinterpret_cast<HWND>(wparam) != hwnd() || LOWORD(lparam) != HTCLIENT || !enabled()) {
            return std::nullopt;
        }
        POINT point{};
        // The live cursor position is unavailable on an inactive desktop.
        const auto position = GetCursorPos(&point) && ScreenToClient(hwnd(), &point)
            ? to_dips(point) : pointer_position_from_screen(GetMessagePos());
        const auto* editor = dynamic_cast<TextBox*>(tree_.hit_test(position));
        if (!editor || !editor->enabled()) { return std::nullopt; }
        SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        return TRUE;
    }
    case WM_CHAR: {
        const wchar_t unit = static_cast<wchar_t>(wparam);
        if (enabled() && unit >= L' ' && tree_.text_input({&unit, 1})) { return 0; }
        return std::nullopt;
    }
    case WM_GETDLGCODE: {
        if (wparam == VK_TAB) {
            const bool backwards = key_down(VK_SHIFT);
            return tree_.next_focus(backwards, !outside_tab_stop(backwards)) ? DLGC_WANTTAB : 0;
        }
        return tree_.focused() ? DLGC_WANTARROWS | DLGC_WANTCHARS |
            ((wparam == VK_RETURN || wparam == VK_ESCAPE) ? DLGC_WANTMESSAGE : 0) : 0;
    }
    case WM_SYSKEYDOWN: case WM_SYSKEYUP:
    case WM_KEYDOWN: case WM_KEYUP: {
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        if (down) { hide_tooltip(); }
        auto* before = tree_.focused();
        const bool handled = enabled() && tree_.key({static_cast<unsigned>(wparam), down,
            key_down(VK_SHIFT), key_down(VK_CONTROL), key_down(VK_MENU), lparam});
        // Keyboard focus that moved to another element shows its tooltip.
        if (down && tree_.focused() != before) { follow_tooltip(tooltip_owner(tree_.focused()), true); }
        if (handled) { return 0; }
        return std::nullopt;
    }
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        hide_tooltip();
        if (!enabled()) { return std::nullopt; }
        event.kind = Kind::wheel;
        event.position = pointer_position_from_screen(lparam);
        event.wheelDelta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / WHEEL_DELTA;
        if (message == WM_MOUSEHWHEEL) {
            event.wheelAxis = layout::Axis::horizontal;
            event.wheelDelta = -event.wheelDelta;
        }
        return tree_.pointer(event) ? std::optional<LRESULT>{0} : std::nullopt;
    case WM_MOUSEMOVE: event.kind = Kind::move; break;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: event.kind = Kind::down; event.button = Button::left; break;
    case WM_LBUTTONUP: event.kind = Kind::up; event.button = Button::left; break;
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: event.kind = Kind::down; event.button = Button::middle; break;
    case WM_MBUTTONUP: event.kind = Kind::up; event.button = Button::middle; break;
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: event.kind = Kind::down; event.button = Button::right; break;
    case WM_RBUTTONUP: event.kind = Kind::up; event.button = Button::right; break;
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: case WM_XBUTTONUP:
        event.kind = message == WM_XBUTTONUP ? Kind::up : Kind::down;
        event.button = GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? Button::x1 : Button::x2;
        break;
    default: return std::nullopt;
    }
    if (!enabled()) { return std::nullopt; }
    event.position = pointer_position(lparam);
    if (event.kind == Kind::down) { hide_tooltip(); }
    // A press takes the keyboard from a window inside this one, unless it lands on an element that
    // keeps out of the focus a press gives.
    if (event.kind == Kind::down && event.button == Button::left && !focused() && !tree_.press_keeps_focus(event.position)) {
        pointerFocus_ = true;
        const auto reset = wil::scope_exit([&] { pointerFocus_ = false; });
        focus();
    }
    const bool handled = tree_.pointer(event);
    if (event.kind == Kind::move) {
        lastPointer_ = event.position;
        follow_tooltip(tooltip_owner(tree_.hovered_), false);
    }
    if (handled) { return event.button == Button::x1 || event.button == Button::x2 ? TRUE : 0; }
    return std::nullopt;
}

void ElementWindow::on_enabled(bool enabled) {
    if (!enabled) { tree_.release_capture(); tree_.focus(nullptr); }
    if (hwnd()) { invalidate(); }
    if (automation_) { automation_->geometryDirty = true; ElementAutomation::refresh(*this); }
}

void ElementWindow::on_hover(bool inside) {
    if (!inside) {
        tree_.pointer_leave();
        if (!tipKeyboard_) { follow_tooltip(nullptr, false); }
    }
}
void ElementWindow::on_capture_lost() { tree_.release_capture(); }

}
