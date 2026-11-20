#include <composia/ElementWindow.hpp>

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
    : Window(app, title, widthDip, heightDip, Content::composition, parent),
      tree_(std::move(root), [this] {
          if (hwnd()) { THROW_IF_WIN32_BOOL_FALSE(PostMessageW(hwnd(), update_message(), 0, 0)); }
      }),
      target_(*this, !parent || CompositionWindowTarget::hosts_children(parent)
          ? CompositionWindowTarget::Hosting::shared : CompositionWindowTarget::Hosting::standalone) {
    on_resize();
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
    if (!value) { tree_.focus(nullptr); }
    else if (!pointerFocus_ && !tree_.focused()) { tree_.move_focus(key_down(VK_SHIFT)); }
}

std::optional<LRESULT> ElementWindow::on_message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == update_message()) {
        if (const auto region = tree_.take_dirty_bounds()) { invalidate(*region); }
        return 0;
    }
    using Kind = PointerEvent::Kind;
    using Button = PointerEvent::Button;
    PointerEvent event{};
    switch (message) {
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
    case WM_KEYDOWN: case WM_KEYUP:
        if (enabled() && tree_.key({static_cast<unsigned>(wparam), message == WM_KEYDOWN,
                key_down(VK_SHIFT), key_down(VK_CONTROL), key_down(VK_MENU)})) { return 0; }
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

}
