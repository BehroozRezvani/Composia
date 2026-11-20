#include <composia/ElementWindow.hpp>
#include <composia/NativeControl.hpp>
#include <composia/ScopedSurfaceDraw.hpp>
#include "support/TestSupport.hpp"

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
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"pixels", pixels}, {"navigation", navigation}});
}
