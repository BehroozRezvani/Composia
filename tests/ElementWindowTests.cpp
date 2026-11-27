#include <composia/Button.hpp>
#include <composia/ElementWindow.hpp>
#include <composia/HostedVisual.hpp>
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
    std::function<bool(const ContextMenuEvent&)> menu;
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
    bool on_context_menu(const ContextMenuEvent& e) override { return menu && menu(e); }
};

// Draws a surface of its own from its layout hook, as an element showing a picture in the
// compositor does when its size changes.
class SelfDrawn final : public Element {
public:
    explicit SelfDrawn(Application& app) : app_(app) { observe_layout(); }
    composition::CompositionDrawingSurface surface{nullptr};
    unsigned draws{};

private:
    void on_layout_updated() override {
        if (!surface) {
            surface = app_.graphics().composition_device().CreateDrawingSurface({8, 8},
                winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
                winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
        }
        app_.render([&] {
            ScopedSurfaceDraw draw{surface, app_.graphics(), 96};
            draw.context()->Clear(D2D1::ColorF(0x30A050));
            draw.finish();
        });
        ++draws;
    }
    Application& app_;
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

int visuals(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>(ElementLayout{Kind::stack});
        auto before = std::make_shared<Tile>(0x203040);
        before->set_preferred_size({100, 20});
        auto scroll = std::make_shared<Element>(ElementLayout{Kind::scroll, layout::Axis::vertical, 0, 1, 100.f});
        auto content = std::make_shared<Element>(ElementLayout{Kind::stack});
        auto spacer = std::make_shared<Element>();
        spacer->set_preferred_size({100, 30});
        // The visual lies over a tile, which keeps the pointer.
        auto layer = std::make_shared<Element>(ElementLayout{Kind::overlay});
        auto under = std::make_shared<Tile>(0x406080);
        under->set_preferred_size({120, 40});
        auto tail = std::make_shared<Element>();
        tail->set_preferred_size({100, 200});
        const auto sprite = app.compositor().CreateSpriteVisual();
        sprite.RelativeSizeAdjustment({1, 1});
        sprite.Brush(app.compositor().CreateColorBrush({255, 255, 0, 0}));
        auto host = std::make_shared<HostedVisual>(sprite);
        layer->add(under);
        layer->add(host);
        content->add(spacer);
        content->add(layer);
        content->add(tail);
        scroll->add(content);
        root->add(before);
        root->add(scroll);
        ElementWindow window{app, L"Hosted visuals", 200, 160, root};
        window.show(SW_SHOWNOACTIVATE);
        flush(window);

        // The scroll viewport spans 20 to 120 DIPs; the visual's element starts 30 DIPs into it.
        const auto frame = sprite.Parent();
        require(frame && frame.Parent() == window.composition_target().root() && frame.IsVisible() &&
            frame.Offset().y == 50 && frame.Size().y == 40 && frame.Size().x == window.client_bounds().width,
            "Hosted visual placement");
        const auto clip = frame.Clip().as<composition::InsetClip>();
        require(clip.TopInset() == 0 && clip.BottomInset() == 0, "An unclipped hosted visual was clipped");
        require(window.elements().hit_test({10, 60}) == under.get(), "A hosted visual took the pointer");
        scroll->set_scroll_offset(40);
        flush(window);
        require(frame.Offset().y == 10 && clip.TopInset() == 10 && clip.BottomInset() == 0 && frame.IsVisible(),
            "Scrolling did not move the hosted visual or clip it to the viewport");
        scroll->set_scroll_offset(80);
        flush(window);
        require(!frame.IsVisible(), "A scrolled-out hosted visual was left visible");
        scroll->set_scroll_offset(0);
        flush(window);
        require(frame.IsVisible() && frame.Offset().y == 50 && clip.TopInset() == 0, "Revealing a hosted visual kept its old place");
        layer->remove(*host);
        flush(window);
        require(!frame.IsVisible(), "A detached hosted visual was left visible");
        host.reset();
        require(!frame.Parent() && !sprite.Parent(), "A destroyed hosted visual left its visual in the window");
    }
    return 0;
}

int snapshot(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>(ElementLayout{Kind::stack});
        auto top = std::make_shared<Tile>(0x203040);
        top->set_preferred_size({100, 20});
        auto bottom = std::make_shared<Tile>(0x406080);
        bottom->set_preferred_size({100, 40});
        root->add(top);
        root->add(bottom);
        ElementWindow window{app, L"Snapshots", 200, 100, root};
        window.show(SW_SHOWNOACTIVATE);
        flush(window);
        // From 10 DIPs down: the top tile's last 10 DIPs, then the bottom tile.
        const auto sprite = window.snapshot({10, 10, 50, 30});
        require(sprite.Size().x == 50 && sprite.Size().y == 30 && !sprite.Parent(), "Snapshot size");
        const auto surface = sprite.Brush().as<composition::CompositionSurfaceBrush>().Surface();
        const auto px = [&](float dips) { return static_cast<LONG>(std::lround(dips * window.scale())); };
        require(testing::matches(testing::surface_pixel(app, surface, px(2), px(2)), 0x203040) &&
            testing::matches(testing::surface_pixel(app, surface, px(2), px(20)), 0x406080),
            "The snapshot did not picture the area");
        top->color = 0xc03020;
        top->invalidate();
        flush(window);
        require(testing::matches(testing::surface_pixel(app, surface, px(2), px(2)), 0x203040),
            "The snapshot followed a later change");
        bool rejected{};
        try { (void)window.snapshot({0, 0, 0, 10}); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "An empty area was accepted");
    }
    return 0;
}

// Layout runs before a window's surface is opened for drawing, so a layout hook can draw a surface
// of its own (only one can be drawn at a time) even when the layout is still pending at the paint.
int layout_draws(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>(ElementLayout{Kind::stack});
        auto drawn = std::make_shared<SelfDrawn>(app);
        drawn->set_preferred_size({50, 10});
        auto tile = std::make_shared<Tile>(0x203040);
        tile->set_preferred_size({100, 20});
        root->add(drawn);
        root->add(tile);
        ElementWindow window{app, L"Layout that draws", 200, 100, root};
        window.show(SW_SHOWNOACTIVATE);
        flush(window);
        const auto before = drawn->draws;
        // The change is still waiting for the tree's update message when the window paints.
        drawn->set_preferred_size({60, 12});
        window.invalidate({0, 0, 100, 40});
        UpdateWindow(window.hwnd());
        window.rethrow_callback_error();
        require(drawn->draws > before, "The layout hook did not run for the paint");
        require(testing::matches(testing::surface_pixel(app, drawn->surface, 1, 1), 0x30A050), "The hook's surface was not drawn");
        const auto px = [&](float dips) { return static_cast<LONG>(std::lround(dips * window.scale())); };
        require(testing::matches(testing::surface_pixel(app, window.composition_target().surface(), px(2), px(16)), 0x203040),
            "The window was not painted after the hook drew");
        const auto painted = drawn->draws;
        drawn->set_preferred_size({70, 14});
        const auto sprite = window.snapshot({0, 0, 100, 40});
        require(drawn->draws > painted && sprite.Size().x == 100, "The layout hook did not run for the snapshot");
        flush(window);
    }
    app.close();
    return 0;
}

// A window destroyed while a hosted window inside it has the focus: as the tree is released the
// hosted window hides and hands the focus back to the window, which must not then walk elements
// that already left the tree (here the focusable one before it).
int hosted_teardown(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>(ElementLayout{Kind::stack});
        auto before = std::make_shared<Tile>(0x203040);
        before->set_preferred_size({100, 20});
        before->set_focusable(true);
        root->add(before);
        auto window = std::make_unique<ElementWindow>(app, L"Hosted teardown", 200, 120, root);
        NativeControl edit{*window, L"EDIT", WS_TABSTOP | WS_VISIBLE | ES_AUTOHSCROLL};
        auto host = std::make_shared<HostedWindow>(edit.hwnd());
        host->set_preferred_size({120, 30});
        root->add(host);
        window->show(SW_SHOWNORMAL);
        flush(*window);
        // The focus goes to the control itself, as a click or UI Automation gives it, from the
        // element before it, which the window then remembers.
        require(window->focus_element(*before), "Focus the element before the hosted window");
        flush(*window);
        SetFocus(edit.hwnd());
        require(GetFocus() == edit.hwnd(), "The hosted window did not take the focus");
        // A press on an element that keeps out of the focus a press gives leaves it in the control.
        before->set_focus_on_press(false);
        const auto at = [&](float x, float y) {
            return MAKELPARAM(std::lround(x * window->scale()), std::lround(y * window->scale()));
        };
        SendMessageW(window->hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, at(5, 5));
        SendMessageW(window->hwnd(), WM_LBUTTONUP, 0, at(5, 5));
        require(GetFocus() == edit.hwnd() && !before->focused(), "A press took the focus from the hosted window");
        window.reset();
        require(!IsWindow(edit.hwnd()), "The hosted window outlived its window");
        require(!before->focused() && !host->focused(), "An element kept the focus of a destroyed window");
    }
    app.close();
    return 0;
}

// The focus cue: hidden at first, shown by a key press in the window or a window inside it, hidden
// by a press of a mouse button, and shared with the windows inside.
int focus_cues(const testing::Options& options) {
    Application app{options.warp};
    {
        auto root = std::make_shared<Element>(ElementLayout{Kind::stack});
        auto first = std::make_shared<Tile>(0x203040);
        first->set_preferred_size({100, 20});
        first->set_focusable(true);
        root->add(first);
        ElementWindow window{app, L"Focus cues", 200, 120, root};
        NativeControl edit{window, L"EDIT", WS_TABSTOP | WS_VISIBLE | ES_AUTOHSCROLL};
        Button child{window, L"Child", [](ScopedSurfaceDraw&, numerics::float2, const Button::State&) {}};
        window.show(SW_SHOWNORMAL);
        flush(window);
        require(window.focus_element(*first), "Focus an element");
        require(!window.focus_visible() && !first->focus_visible(), "The focus showed before any key");
        SendMessageW(window.hwnd(), WM_KEYDOWN, VK_DOWN, 0);
        require(window.focus_visible() && first->focus_visible() && child.focus_visible(), "A key did not show the focus, here and inside");
        SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(150, 100));
        SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(150, 100));
        require(!window.focus_visible() && !child.focus_visible(), "A click did not hide the focus");
        // A key in a window inside shows it for the whole window.
        SendMessageW(child.hwnd(), WM_KEYDOWN, VK_TAB, 0);
        require(window.focus_visible() && first->focus_visible(), "A key in a window inside did not show the focus");
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

        require(!window.tooltip_hwnd(), "A tooltip window existed before any tooltip showed");
        window.set_dark_tooltips(true);   // Kept for the tooltip window made later.
        SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0, at(10, 10));
        require(!pump_until(shown_tip, delay * 2), "An element without a tooltip showed one");
        SendMessageW(window.hwnd(), WM_MOUSEMOVE, 0, at(10, 40));
        require(!shown_tip(), "The tooltip showed before the hover time");
        require(pump_until(shown_tip, delay * 4) && tip_text(shown_tip()) == L"Archive (Backspace)",
            "Resting on the element did not show its tooltip");
        require(window.tooltip_hwnd() == shown_tip(), "tooltip_hwnd() is not the tooltip shown");
        window.set_dark_tooltips(false);
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

        // WM_CONTEXTMENU reaches the tree: a click by screen position, the keyboard by (-1, -1).
        std::vector<ContextMenuEvent> menus;
        archive->menu = [&](const ContextMenuEvent& e) { menus.push_back(e); return true; };
        POINT screen{static_cast<LONG>(10 * window.scale()), static_cast<LONG>(40 * window.scale())};
        ClientToScreen(window.hwnd(), &screen);
        require(SendMessageW(window.hwnd(), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(window.hwnd()),
            MAKELPARAM(screen.x, screen.y)) == 0 && menus.size() == 1 && !menus[0].keyboard &&
            std::abs(menus[0].position.y - 10) < 1, "A right click's WM_CONTEXTMENU missed the element");
        SendMessageW(window.hwnd(), WM_CONTEXTMENU, reinterpret_cast<WPARAM>(window.hwnd()), static_cast<LPARAM>(-1));
        require(menus.size() == 2 && menus[1].keyboard, "Shift+F10's WM_CONTEXTMENU missed the focused element");
    }
    app.close();
    return 0;
}
}

// A window that keeps out of the focus, as a popup that leaves it with its owner: presses reach
// its elements but focus neither the window nor them.
int focus_keeping(const testing::Options& options) {
    Application app{options.warp};
    {
        struct Keeper final : ElementWindow {
            using ElementWindow::ElementWindow;
            bool takes_focus() const noexcept override { return false; }
        };
        auto mainRoot = std::make_shared<Element>(ElementLayout{Kind::stack});
        auto field = std::make_shared<Tile>(0x203040);
        field->set_preferred_size({100, 20});
        field->set_focusable(true);
        mainRoot->add(field);
        ElementWindow main{app, L"Owner", 200, 120, mainRoot};
        auto popupRoot = std::make_shared<Element>(ElementLayout{Kind::stack});
        auto choice = std::make_shared<Tile>(0x406080);
        choice->set_preferred_size({100, 20});
        choice->set_focusable(true);
        popupRoot->add(choice);
        Window::Options options;
        options.style = WS_POPUP;
        options.extendedStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
        options.owner = main.hwnd();
        Keeper popup{app, L"Choices", 120, 60, popupRoot, options};
        main.show(SW_SHOWNORMAL);
        popup.show(SW_SHOWNOACTIVATE);
        flush(main);
        flush(popup);
        require(main.focus_element(*field), "The owner did not take the focus");
        const auto at = [&](float x, float y) {
            return MAKELPARAM(static_cast<int>(x * popup.scale()), static_cast<int>(y * popup.scale()));
        };
        SendMessageW(popup.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, at(10, 10));
        SendMessageW(popup.hwnd(), WM_LBUTTONUP, 0, at(10, 10));
        require(choice->lastPress.x == 10 && !choice->focused() && !popup.elements().focused(), "A press focused an element of the popup");
        require(GetFocus() == main.hwnd() && field->focused(), "A press in the popup took the owner's focus");
        require(!popup.focus_element(*choice) && !choice->focused(), "focus_element focused a window that keeps out of the focus");
    }
    app.close();
    return 0;
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"pixels", pixels}, {"navigation", navigation}, {"hosted", hosted}, {"focus-keeping", focus_keeping},
        {"tooltips", tooltips}, {"visuals", visuals}, {"snapshot", snapshot}, {"hosted-teardown", hosted_teardown},
        {"focus-cues", focus_cues}, {"layout-draws", layout_draws}});
}
