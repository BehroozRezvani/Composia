#include "DemoWindow.hpp"
#include "SmokeTest.hpp"
#include <iostream>
#include <string_view>

class SmokeWindow final : public DemoWindow {
public:
    explicit SmokeWindow(composia::Application& app) : DemoWindow(app), smoke_(*this) {
        THROW_LAST_ERROR_IF(SetTimer(hwnd(), 1, 300, nullptr) == 0);
    }
    ~SmokeWindow() override { if (hwnd()) { KillTimer(hwnd(), 1); } }

private:
    std::optional<LRESULT> on_message(UINT message, WPARAM wparam, LPARAM lparam) override {
        if (message == WM_TIMER && wparam == 1) { smoke_.tick(); return 0; }
        return DemoWindow::on_message(message, wparam, lparam);
    }
    SmokeTest smoke_;
};

int main(int argc, char** argv) {
    try {
        const bool warp = argc > 1 && std::string_view{argv[1]} == "--warp";
        composia::Application app{warp};
        int result{};
        {
            SmokeWindow window{app};
            window.show();
            result = app.run();
        }
        app.close();
        return result;
    } catch (const winrt::hresult_error& error) { std::cerr << winrt::to_string(error.message()) << '\n'; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; }
    return 1;
}
