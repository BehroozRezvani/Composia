#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
void require(bool value, const char* message) { if (!value) { throw std::runtime_error(message); } }
BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
    auto& monitors = *reinterpret_cast<std::vector<HMONITOR>*>(context);
    monitors.push_back(monitor);
    return TRUE;
}
}

int main() {
    try {
        composia::Application app{true};
        {
            composia::Window window{app, L"Monitor transition test", 420, 260};
            composia::Button button{window, L"DPI layout"};
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
                    require(std::abs(logical.x * scale - pixels.cx) < 0.1f, "Composition width diverged from HWND width");
                    std::cout << "monitor_dpi=" << dpi << " child_pixels=" << pixels.cx << ',' << pixels.cy << '\n';
                }
                std::cout << "monitors=" << monitors.size() << " distinct_dpi=" << observedDpi.size() << '\n';
                PostMessageW(window.hwnd(), WM_CLOSE, 0, 0);
            }), "Could not schedule monitor checks");
            require(app.run() == 0, "Monitor test loop failed");
        }
        app.close();
        return 0;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
