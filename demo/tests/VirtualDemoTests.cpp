#include "VirtualDemoWindow.hpp"
#include "support/TestSupport.hpp"
#include <iostream>

// The virtual atlas demo: keyboard, scroll bar, zoom, and drag navigation, resize, minimize, and
// device replacement.
using namespace composia;
using testing::require;

namespace {
int navigation(const testing::Options& options) {
    Application app{options.warp};
    {
        VirtualDemoWindow window{app};
        window.show();
        const auto paint = [&] { UpdateWindow(window.hwnd()); window.rethrow_callback_error(); };
        const auto key = [&](WPARAM code) { SendMessageW(window.hwnd(), WM_KEYDOWN, code, 0); paint(); };
        paint();
        require(window.document().cached_tiles() > 0, "Demo did not draw initial viewport");
        key(VK_END);
        require(window.origin().x > 1000000 && window.origin().y > 1000000, "Far corner navigation failed");
        key(VK_HOME);
        require(window.origin().x == 0 && window.origin().y == 0, "Home navigation failed");
        key(VK_NEXT);
        require(window.origin().y > 100, "Page scroll failed");
        SendMessageW(window.hwnd(), WM_HSCROLL, SB_BOTTOM, 0);
        paint();
        require(window.origin().x > 1000000, "Native scrollbar failed");
        key(VK_OEM_MINUS);
        require(window.zoom() < 1, "Zoom out failed");
        for (int i = 0; i < 15; ++i) { key(VK_OEM_MINUS); }
        require(window.zoom() >= 0.25f && window.document().cached_tiles() <= 256, "Zoom out exceeded cache budget");
        SendMessageW(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(300, 220));
        const auto beforeDrag = window.origin().x;
        SendMessageW(window.hwnd(), WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(350, 250));
        SendMessageW(window.hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(350, 250));
        paint();
        require(window.origin().x < beforeDrag, "Dragging did not pan");
        window.set_bounds({40, 40, 620, 460});
        paint();
        window.show(SW_MINIMIZE);
        window.show(SW_RESTORE);
        paint();
        app.graphics().recreate();
        paint();
        require(window.document().cached_tiles() > 0, "Demo recovery left empty tiles");
        SendMessageW(window.hwnd(), WM_CLOSE, 0, 0);
        window.rethrow_callback_error();
        std::cout << "demo_scroll=true zoom=true drag=true resize=true recovery=true\n";
    }
    app.close();
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {{"navigation", navigation}});
}
