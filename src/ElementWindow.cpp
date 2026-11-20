#include <composia/ElementWindow.hpp>
#include <composia/TextBox.hpp>
#include "ElementAutomation.hpp"

namespace composia {
namespace {
UINT update_message() {
    static const auto id = RegisterWindowMessageW(L"Composia.ElementWindow.Update");
    THROW_LAST_ERROR_IF(!id);
    return id;
}

bool key_down(int key) { return (GetKeyState(key) & 0x8000) != 0; }
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
    destroying_ = on_destroy([this] { ElementAutomation::disconnect(*this); tree_.release_window(); });
    on_resize();
}

ElementWindow::~ElementWindow() { ElementAutomation::disconnect(*this); tree_.release_window(); }

bool ElementWindow::focus_element(Element& element) {
    if (!hwnd() || !enabled() || !tree_.focus(&element)) { return false; }
    focus();
    return focused() && tree_.focused() == &element;
}

void ElementWindow::on_resize() {
    const auto b = client_bounds();
    tree_.set_viewport({b.width, b.height});
    invalidate();
}

void ElementWindow::on_paint() {
    target_.render(*this, [&](ScopedSurfaceDraw& draw, numerics::float2 size) {
        tree_.set_viewport({size.x, size.y});
        tree_.paint(draw);
    });
}

void ElementWindow::on_focus(bool value) {
    if (!value) {
        tree_.release_capture();
        previousFocus_ = tree_.reference(tree_.focused());
        tree_.focus(nullptr);
        if (auto previous = previousFocus_.lock(); previous && !focused()) {
            ElementAutomation::focus_changed(*previous, false);
        }
    }
    else if (auto current = tree_.reference(tree_.focused())) {
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
        ElementAutomation::refresh(*this);
        return 0;
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
            const auto root = GetAncestor(hwnd(), GA_ROOT);
            const auto other = GetNextDlgTabItem(root, hwnd() == root ? nullptr : hwnd(), backwards);
            return tree_.next_focus(backwards, !other || other == hwnd()) ? DLGC_WANTTAB : 0;
        }
        return tree_.focused() ? DLGC_WANTARROWS | DLGC_WANTCHARS |
            ((wparam == VK_RETURN || wparam == VK_ESCAPE) ? DLGC_WANTMESSAGE : 0) : 0;
    }
    case WM_SYSKEYDOWN: case WM_SYSKEYUP:
    case WM_KEYDOWN: case WM_KEYUP:
        if (enabled() && tree_.key({static_cast<unsigned>(wparam), message == WM_KEYDOWN || message == WM_SYSKEYDOWN,
                key_down(VK_SHIFT), key_down(VK_CONTROL), key_down(VK_MENU), lparam})) { return 0; }
        return std::nullopt;
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
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
    if (event.kind == Kind::down && event.button == Button::left && !focused()) {
        pointerFocus_ = true;
        const auto reset = wil::scope_exit([&] { pointerFocus_ = false; });
        focus();
    }
    if (tree_.pointer(event)) { return event.button == Button::x1 || event.button == Button::x2 ? TRUE : 0; }
    return std::nullopt;
}

void ElementWindow::on_enabled(bool enabled) {
    if (!enabled) { tree_.release_capture(); tree_.focus(nullptr); }
    if (hwnd()) { invalidate(); }
    if (automation_) { automation_->geometryDirty = true; ElementAutomation::refresh(*this); }
}

void ElementWindow::on_hover(bool inside) { if (!inside) { tree_.pointer_leave(); } }
void ElementWindow::on_capture_lost() { tree_.release_capture(); }

}
