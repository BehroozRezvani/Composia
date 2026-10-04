#include <composia/Application.hpp>
#include <functional>
#include <iostream>
#include <stdexcept>

namespace {
constexpr UINT actionMessage = WM_APP + 1;

class TestWindow final : public composia::Window {
public:
    explicit TestWindow(composia::Application& app) : Window(app, L"Window lifetime test", 320, 240) {}
    std::function<void()> action;
    std::function<void()> timer;

private:
    std::optional<LRESULT> on_message(UINT message, WPARAM, LPARAM) override {
        if (message == actionMessage && action) {
            action();
            return 0;
        }
        if (message == WM_TIMER && timer) {
            timer();
            return 0;
        }
        return std::nullopt;
    }
};

void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}

void secondary_close() {
    composia::Application app{true};
    TestWindow primary{app};
    TestWindow secondary{app};
    bool primaryClosed = false;
    primary.timer = [&] {
        KillTimer(primary.hwnd(), 1);
        primaryClosed = true;
        DestroyWindow(primary.hwnd());
    };
    THROW_LAST_ERROR_IF(SetTimer(primary.hwnd(), 1, 40, nullptr) == 0);
    THROW_IF_WIN32_BOOL_FALSE(PostMessageW(secondary.hwnd(), WM_CLOSE, 0, 0));
    require(app.run() == 0, "Unexpected exit code");
    require(primaryClosed, "Closing a secondary window terminated the application");
}

void secondary_error() {
    composia::Application app{true};
    TestWindow primary{app};
    TestWindow secondary{app};
    secondary.action = [] { throw std::runtime_error("secondary callback error"); };
    THROW_IF_WIN32_BOOL_FALSE(PostMessageW(secondary.hwnd(), actionMessage, 0, 0));
    bool propagated = false;
    try { (void)app.run(); }
    catch (const std::runtime_error& error) { propagated = std::string_view(error.what()) == "secondary callback error"; }
    require(propagated, "The secondary window's exception was not propagated");
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Expected a test name");
        if (std::string_view(argv[1]) == "secondary-close") { secondary_close(); }
        else if (std::string_view(argv[1]) == "secondary-error") { secondary_error(); }
        else { throw std::runtime_error("Unknown test"); }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
