#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include "support/TestSupport.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <vector>

// Per-monitor DPI: a window moved across every attached monitor, and the DPI change messages a
// window receives when its monitor's scale changes.
using namespace composia;
using testing::require;

namespace {
BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    reinterpret_cast<std::vector<HMONITOR>*>(context)->push_back(monitor);
    return TRUE;
}

class Counter final : public Window {
public:
    Counter(Application& app, HWND parent = nullptr) : Window(app, L"DPI messages", 300, 200, parent) {}
    unsigned resizes{};

private:
    void on_resize() override { ++resizes; }
};

int monitors(const testing::Options&) {
    Application app{true};
    {
        Window window{app, L"Monitor transition test", 420, 260};
        Button button{window, L"DPI layout"};
        std::vector<HMONITOR> monitors;
        THROW_IF_WIN32_BOOL_FALSE(EnumDisplayMonitors(nullptr, nullptr, collect_monitor, reinterpret_cast<LPARAM>(&monitors)));
        require(!monitors.empty(), "No desktop monitor was found");
        std::set<UINT> observedDpi;
        window.show();
        require(app.post([&] {
            for (const auto monitor : monitors) {
                MONITORINFO info{};
                info.cbSize = sizeof(info);
                THROW_IF_WIN32_BOOL_FALSE(GetMonitorInfoW(monitor, &info));
                THROW_IF_WIN32_BOOL_FALSE(SetWindowPos(window.hwnd(), nullptr, info.rcWork.left + 8, info.rcWork.top + 8,
                    std::min(420L, info.rcWork.right - info.rcWork.left - 16),
                    std::min(260L, info.rcWork.bottom - info.rcWork.top - 16), SWP_NOZORDER | SWP_NOACTIVATE));
                require(MonitorFromWindow(window.hwnd(), MONITOR_DEFAULTTONEAREST) == monitor, "Window did not move to the requested monitor");
                const auto dpi = window.dpi();
                observedDpi.insert(dpi);
                require(dpi != 0 && button.dpi() == dpi, "Parent and child DPI diverged after moving monitors");
                button.set_bounds({10.5f, 12.5f, 181.5f, 47.5f});
                UpdateWindow(button.hwnd());
                const auto pixels = button.client_pixels();
                const auto scale = static_cast<float>(dpi) / 96.0f;
                const auto expectedWidth = std::round(192.0f * scale) - std::round(10.5f * scale);
                const auto expectedHeight = std::round(60.0f * scale) - std::round(12.5f * scale);
                require(pixels.cx == expectedWidth && pixels.cy == expectedHeight, "Child bounds did not scale at monitor DPI");
                const auto logical = button.composition_target().logical_size();
                require(std::abs(logical.x * scale - static_cast<float>(pixels.cx)) < 0.1f, "Composition width diverged from HWND width");
                std::cout << "monitor_dpi=" << dpi << " child_pixels=" << pixels.cx << ',' << pixels.cy << '\n';
            }
            std::cout << "monitors=" << monitors.size() << " distinct_dpi=" << observedDpi.size() << '\n';
            PostMessageW(window.hwnd(), WM_CLOSE, 0, 0);
        }), "Could not schedule monitor checks");
        require(app.run() == 0, "Monitor test loop failed");
    }
    app.close();
    return 0;
}

// Windows sends WM_DPICHANGED to a top-level window with the rectangle it suggests at the new
// scale, and WM_DPICHANGED_AFTERPARENT to each child. These are sent here directly, since moving
// to a monitor with another scale needs a second monitor; the DPI itself does not change.
int messages(const testing::Options&) {
    Application app{true};
    {
        Counter top{app};
        Counter child{app, top.hwnd()};
        top.show(SW_SHOWNOACTIVATE);
        const auto topResizes = top.resizes, childResizes = child.resizes;
        RECT suggested{120, 140, 120 + 450, 140 + 300};
        SendMessageW(top.hwnd(), WM_DPICHANGED, MAKEWPARAM(144, 144), reinterpret_cast<LPARAM>(&suggested));
        RECT actual{};
        THROW_IF_WIN32_BOOL_FALSE(GetWindowRect(top.hwnd(), &actual));
        require(EqualRect(&actual, &suggested) != FALSE, "The window did not take the suggested rectangle");
        require(top.resizes > topResizes, "A DPI change did not reach on_resize");
        SendMessageW(child.hwnd(), WM_DPICHANGED_AFTERPARENT, 0, 0);
        require(child.resizes > childResizes, "A child's DPI change did not reach on_resize");
        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(top.hwnd()));
    }
    app.close();
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {
        {"monitors", monitors},
        {"messages", messages},
    });
}
