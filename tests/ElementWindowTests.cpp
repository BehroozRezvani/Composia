#include <composia/ElementWindow.hpp>
#include <composia/HostedWindow.hpp>
#include <composia/NativeControl.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include "support/TestSupport.hpp"
#include <chrono>
#include <cmath>
#include <commctrl.h>
#include <optional>

using namespace composia;
using testing::require;
using Kind = ElementLayout::Kind;

namespace {
class Tile : public Element {
public:
    explicit Tile(UINT32 color) : color(color) {}
    UINT32 color;
    unsigned paints{};
    layout::Point lastPress{};
    unsigned lastKey{};

private:
    void paint(ScopedSurfaceDraw& draw) override {
        ++paints;
        // Deliberately overdraw all edges. The host installs the local transform and clip.
        draw.context()->FillRectangle({-100, -100, bounds().width + 100, bounds().height + 100}, draw.solid_brush(color));
    }
    bool on_pointer(const PointerEvent& e) override {
        if (e.kind != PointerEvent::Kind::down) { return false; }
        lastPress = e.position;
        return true;
    }
    bool on_key(const KeyEvent& e) override {
        if (e.key == VK_TAB) { return false; }
        lastKey = e.key;
        return true;
    }
};

void flush(ElementWindow& window) {
    MSG msg{};
    while (PeekMessageW(&msg, window.hwnd(), 0xC000, 0xFFFF, PM_REMOVE)) {
        DispatchMessageW(&msg);
    }
    UpdateWindow(window.hwnd());
    window.rethrow_callback_error();
}

int pixels(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>(ElementLayout{Kind::stack, layout::Axis::horizontal, 4});
        auto panel = std::make_shared<Element>();
        panel->set_preferred_size({40, 20});
        auto back = std::make_shared<Tile>(0xAA0000);
        auto front = std::make_shared<Tile>(0x0000FF);
        auto side = std::make_shared<Tile>(0x00AA00);
        side->set_preferred_size({30, 10});
        front->set_offset({5, 5});
        panel->add(back);
        panel->add(front);
        root->add(panel);
        root->add(side);
        ElementWindow window{app, L"Retained painting", 200, 100, root};
        window.show(SW_SHOWNOACTIVATE);
        flush(window);
        const auto pixel = [&](int x, int y) {
            return testing::surface_pixel(app, window.composition_target().surface(),
                static_cast<LONG>(x * window.scale()), static_cast<LONG>(y * window.scale()));
        };
        require(testing::matches(pixel(2, 2), back->color) && testing::matches(pixel(10, 10), front->color) &&
            pixel(42, 10).a == 0 && testing::matches(pixel(50, 10), side->color), "Local transforms, stacking or ancestor clip");

        const auto before = side->paints;
        front->color = 0xEEC000;
        front->invalidate({2, 2, 10, 10});
        flush(window);
        const auto changed = pixel(10, 10), preserved = pixel(24, 10);
        std::cout << "partial RGB " << changed.r << ',' << changed.g << ',' << changed.b
            << " preserved RGB " << preserved.r << ',' << preserved.g << ',' << preserved.b
            << " sibling paints " << before << " -> " << side->paints << '\n';
        require(testing::matches(changed, front->color), "Dirty pixels kept their old color");
        require(testing::matches(preserved, 0x0000FF), "Partial repaint changed preserved pixels");
        require(side->paints == before, "Partial repaint visited a clean sibling");
        back->set_z_index(1);
        flush(window);
        require(testing::matches(pixel(24, 10), back->color), "Z-order invalidation failed to restore the front layer");
        panel->set_offset({0, 12});
        flush(window);
        require(pixel(20, 6).a == 0 && testing::matches(pixel(20, 16), back->color), "Moving a panel left stale pixels");

        const auto client = window.client_bounds();
        THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(window.hwnd(), nullptr, 0, 0, 320, 220, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
        flush(window);
        require(root->bounds().width != client.width && root->bounds().width == window.client_bounds().width &&
            root->bounds().height == window.client_bounds().height, "WM_SIZE did not arrange the root");
        app.graphics().recreate();
        window.invalidate({2, 2, 4, 4});
        flush(window);
        require(testing::matches(pixel(50, 20), side->color), "Device replacement lost content outside partial invalidation");
    }
    app.close();
    return 0;
}

int navigation(const testing::Options& options) {
    Application app{options.warp};
    {
        Window parent{app, L"Focus traversal", 360, 140};
        auto scroll = std::make_shared<Element>(ElementLayout{Kind::scroll});
        auto list = std::make_shared<Element>(ElementLayout{Kind::stack});
        for (int i = 0; i < 64; ++i) {
            auto row = std::make_shared<Tile>(0x203040);
            row->set_preferred_size({100, 20});
            row->set_focusable(true);
            list->add(row);
        }
        scroll->add(list);
        ElementWindow pane{app, L"Choices", 100, 60, scroll, parent.hwnd()};
        NativeControl edit{parent, L"EDIT", WS_TABSTOP | WS_VISIBLE};
        edit.set_bounds({140, 0, 160, 40});
        parent.show(SW_SHOWNOACTIVATE);
        flush(pane);
        auto& tree = pane.elements();
        const auto first = std::static_pointer_cast<Tile>(list->children().front());
        const auto last = std::static_pointer_cast<Tile>(list->children().back());
        const auto initialPaints = first->paints;
        tree.focus(last.get());
        flush(pane);
        require(initialPaints && last->paints && first->paints == initialPaints, "Scrolling repainted an offscreen row");
        POINT cursor{4, static_cast<LONG>(50 * pane.scale())};
        ClientToScreen(pane.hwnd(), &cursor);
        SendMessageW(pane.hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(cursor.x, cursor.y));
        flush(pane);
        const auto selected = std::static_pointer_cast<Tile>(list->children()[59]);
        edit.focus();
        SendMessageW(pane.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(static_cast<int>(4 * pane.scale()),
            static_cast<int>(20 * pane.scale())));
        require(pane.focused() && selected->focused() && selected->lastPress.x > 0 && selected->lastPress.y < 20,
            "Pointer focus entry lost the scrolled hit or local coordinates");
        SendMessageW(pane.hwnd(), WM_KEYDOWN, 'A', 0);
        require(selected->lastKey == 'A', "Window did not route the key to the logical focus");
        const auto tab = [&] {
            MSG message{};
            message.hwnd = pane.hwnd();
            message.message = WM_KEYDOWN;
            message.wParam = VK_TAB;
            if (!IsDialogMessageW(parent.hwnd(), &message)) { DispatchMessageW(&message); }
            pane.rethrow_callback_error();
        };
        tab();
        require(tree.focused() == list->children()[60].get(), "Tab escaped before the last drawn tab stop");
        tree.focus(last.get());
        tab();
        require(GetFocus() == edit.hwnd() && !tree.focused(), "Tab did not leave the last element for the native control");
    }
    app.close();
    return 0;
}

int hosted(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>(ElementLayout{Kind::stack});
        auto before = std::make_shared<Tile>(0x203040);
        before->set_preferred_size({100, 20});
        before->set_focusable(true);
        auto scroll = std::make_shared<Element>(ElementLayout{Kind::scroll, layout::Axis::vertical, 0, 1, 100.f});
        auto content = std::make_shared<Element>(ElementLayout{Kind::stack});
        auto spacer = std::make_shared<Element>();
        spacer->set_preferred_size({100, 30});
        auto tail = std::make_shared<Element>();
        tail->set_preferred_size({100, 200});
        auto after = std::make_shared<Tile>(0x406080);
        after->set_preferred_size({100, 20});
        after->set_focusable(true);
        content->add(spacer);
        scroll->add(content);
        root->add(before);
        root->add(scroll);
        root->add(after);
        ElementWindow window{app, L"Hosted windows", 200, 160, root};
        NativeControl edit{window, L"EDIT", WS_TABSTOP | WS_VISIBLE | ES_AUTOHSCROLL};
        auto host = std::make_shared<HostedWindow>(edit.hwnd());
        const auto shown = [&] { return (GetWindowLongPtrW(edit.hwnd(), GWL_STYLE) & WS_VISIBLE) != 0; };
        require(!shown(), "A detached hosted window was left visible");
        host->set_preferred_size({120, 40});
        content->add(host);
        content->add(tail);
        window.show(SW_SHOWNOACTIVATE);
        flush(window);

        // The scroll viewport spans 20 to 120 DIPs; the hosted element starts 30 DIPs into it.
        const auto scale = window.scale();
        const auto px = [&](float dips) { return static_cast<LONG>(std::lround(dips * scale)); };
        const auto placed = [&] {
            RECT r{};
            GetWindowRect(edit.hwnd(), &r);
            MapWindowPoints(nullptr, window.hwnd(), reinterpret_cast<POINT*>(&r), 2);
            return r;
        };
        const auto visible_part = [&] {
            RECT r{};
            return GetWindowRgnBox(edit.hwnd(), &r) == ERROR ? std::optional<RECT>{} : std::optional<RECT>{r};
        };
        auto r = placed();
        require(shown() && r.left == 0 && r.top == px(50) && r.right == window.client_pixels().cx &&
            r.bottom == px(90) && !visible_part(), "Hosted window placement");

        scroll->set_scroll_offset(40);
        flush(window);
        r = placed();
        const auto part = visible_part();
        require(r.top == px(10) && r.bottom == px(50) && part && part->left == 0 && part->top == px(20) - px(10) &&
            part->bottom == px(50) - px(10), "Scrolling did not move the hosted window or clip it to the viewport");
        scroll->set_scroll_offset(80);
        flush(window);
        require(!shown(), "A scrolled-out hosted window was left visible");
        scroll->set_scroll_offset(0);
        flush(window);
        require(shown() && placed().top == px(50) && !visible_part(), "Revealing a hosted window kept its old place or region");

        scroll->set_enabled(false);
        flush(window);
        require(!IsWindowEnabled(edit.hwnd()), "A disabled ancestor left the hosted window enabled");
        scroll->set_enabled(true);
        flush(window);
        require(IsWindowEnabled(edit.hwnd()), "Enabling the ancestor left the hosted window disabled");

        // Tab as Application::run delivers it: keys aimed at the root that it wants go straight to it.
        const auto tab = [&](HWND from, bool backwards = false) {
            BYTE keys[256]{};
            GetKeyboardState(keys);
            const auto shift = keys[VK_SHIFT];
            keys[VK_SHIFT] = backwards ? 0x80 : 0;
            SetKeyboardState(keys);
            MSG message{};
            message.hwnd = from;
            message.message = WM_KEYDOWN;
            message.wParam = VK_TAB;
            const bool wanted = from == window.hwnd() &&
                (SendMessageW(from, WM_GETDLGCODE, VK_TAB, reinterpret_cast<LPARAM>(&message)) & DLGC_WANTTAB);
            if (wanted || !IsDialogMessageW(window.hwnd(), &message)) { DispatchMessageW(&message); }
            keys[VK_SHIFT] = shift;
            SetKeyboardState(keys);
            flush(window);
        };
        require(window.focus_element(*before), "Focus before the hosted window");
        tab(window.hwnd());
        require(GetFocus() == edit.hwnd(), "Tab did not enter the hosted window");
        tab(edit.hwnd());
        require(window.focused() && after->focused(), "Tab from the hosted window skipped the next element");
        tab(window.hwnd(), true);
        require(GetFocus() == edit.hwnd(), "Shift+Tab did not return to the hosted window");
        tab(edit.hwnd(), true);
        require(window.focused() && before->focused(), "Shift+Tab from the hosted window skipped the previous element");
        require(window.focus_element(*host) && GetFocus() == edit.hwnd(), "focus_element did not focus the hosted window");

        content->remove(*host);
        flush(window);
        require(!shown() && GetFocus() != edit.hwnd(), "A removed hosted window kept its place or the focus");
    }
    app.close();
    return 0;
}

int tooltips(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>(ElementLayout{Kind::stack});
        auto plain = std::make_shared<Tile>(0x203040);
        auto archive = std::make_shared<Tile>(0x406080);
        for (auto& tile : {plain, archive}) {
            tile->set_preferred_size({100, 30});
            tile->set_focusable(true);
            root->add(tile);
        }
        archive->set_tooltip(L"Archive (Backspace)");
        ElementWindow window{app, L"Tooltips", 200, 100, root};
        window.show(SW_SHOWNOACTIVATE);
        flush(window);

        // The tooltip window belongs to this thread; find the visible one and read its text.
        const auto shown_tip = [] {
            HWND found{};
            EnumThreadWindows(GetCurrentThreadId(), [](HWND hwnd, LPARAM out) {
                wchar_t name[64]{};
                GetClassNameW(hwnd, name, 64);
                if (std::wstring_view{name} == TOOLTIPS_CLASSW && IsWindowVisible(hwnd)) {
                    *reinterpret_cast<HWND*>(out) = hwnd;
                    return FALSE;
                }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&found));
            return found;
        };
        const auto tip_text = [&](HWND tip) {
            wchar_t text[64]{};
            TOOLINFOW tool{};
            tool.cbSize = sizeof(tool);
            tool.hwnd = window.hwnd();
            tool.uId = reinterpret_cast<UINT_PTR>(window.hwnd());
            tool.lpszText = text;
            SendMessageW(tip, TTM_GETTEXTW, 64, reinterpret_cast<LPARAM>(&tool));
            return std::wstring{text};
        };
        // Runs messages, timers and the tooltip's fades included, until the condition holds or the
        // time passes. The pointer moves are synthetic and the real cursor is elsewhere, so the
        // WM_MOUSELEAVE that Windows posts for them is dropped.
        const auto pump_until = [&](auto&& condition, std::chrono::milliseconds limit) {
            const auto end = std::chrono::steady_clock::now() + limit;
            while (!condition() && std::chrono::steady_clock::now() < end) {
                MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT);
                MSG message{};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                    if (message.message != WM_MOUSELEAVE) { DispatchMessageW(&message); }
                }
                window.rethrow_callback_error();
            }
            return condition();
        };
        const auto hidden = [&] { return !shown_tip(); };
        const auto delay = std::chrono::milliseconds{GetDoubleClickTime()};
        const auto at = [&](float x, float y) {
            return MAKELPARAM(static_cast<int>(x * window.scale()), static_cast<int>(y * window.scale()));
        };

        SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0, at(10, 10));
        require(!pump_until(shown_tip, delay * 2), "An element without a tooltip showed one");
        SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0, at(10, 40));
        require(!shown_tip(), "The tooltip showed before the hover time");
        require(pump_until(shown_tip, delay * 4) && tip_text(shown_tip()) == L"Archive (Backspace)",
            "Resting on the element did not show its tooltip");
        RECT r{};
        GetWindowRect(shown_tip(), &r);
        POINT below{0, static_cast<LONG>(40 * window.scale())};
        ClientToScreen(window.hwnd(), &below);
        require(r.top > below.y, "The tooltip covered the pointer");

        SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, at(10, 40));
        SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, at(10, 40));
        require(pump_until(hidden, delay), "A press did not hide the tooltip");
        SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0, at(12, 41));
        require(!pump_until(shown_tip, delay * 2), "The tooltip came back before the pointer left the element");

        SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0, at(10, 80));
        require(window.focus_element(*plain), "Focus the first element");
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_TAB, 0);
        require(archive->focused() && pump_until(shown_tip, delay * 4), "Keyboard focus did not show the tooltip");
        GetWindowRect(shown_tip(), &r);
        POINT bottom{0, static_cast<LONG>(60 * window.scale())};
        ClientToScreen(window.hwnd(), &bottom);
        require(r.top >= bottom.y - 1, "The keyboard tooltip was not placed below the element");
        archive->set_tooltip(L"");
        require(pump_until(hidden, delay), "Clearing the text left the tooltip showing");
    }
    app.close();
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"pixels", pixels}, {"navigation", navigation}, {"hosted", hosted},
        {"tooltips", tooltips}});
}
