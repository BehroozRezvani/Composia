#include <composia/Application.hpp>
#include <composia/Button.hpp>
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

void require_invalid_handle(const std::function<void()>& operation, const char* message) {
    bool rejected{};
    try { operation(); }
    catch (const wil::ResultException& error) {
        rejected = error.GetErrorCode() == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    }
    catch (const winrt::hresult_error& error) {
        rejected = error.code() == HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);
    }
    require(rejected, message);
}

void closed_window() {
    composia::Application app{true};
    {
        TestWindow window{app};
        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(window.hwnd()));
        require(!window.hwnd() && window.dpi() == 0, "Destroyed HWND was retained");
        require_invalid_handle([&] { window.invalidate(); }, "Closed window accepted invalidation");
        require_invalid_handle([&] { window.show(); }, "Closed window accepted showing");
        require_invalid_handle([&] { window.set_bounds({0, 0, 100, 100}); }, "Closed window accepted bounds");
        require_invalid_handle([&] { (void)window.client_pixels(); }, "Closed window returned client bounds");
        require(app.run() == 0, "A closed window kept the application running");
    }
    app.close();
}

void closed_parent() {
    composia::Application app{true};
    {
        TestWindow parent{app};
        composia::Button button{parent, L"Child"};
        require(!button.top_level() && GetParent(button.hwnd()) == parent.hwnd(), "Button was not created as a child");
        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(parent.hwnd()));
        require(!parent.hwnd() && !button.hwnd() && !button.enabled(), "Parent destruction left a live child");
        require_invalid_handle([&] { button.invalidate(); }, "Destroyed child accepted invalidation");
        require_invalid_handle([&] { button.enabled(true); }, "Destroyed child accepted an enabled-state change");
        require_invalid_handle([&] { button.invoke(); }, "Destroyed child accepted invocation");
        require_invalid_handle([&] { composia::Button orphan{parent, L"Orphan"}; }, "Button accepted a destroyed parent");
        require(app.run() == 0, "Rejected child creation left a top-level window");
    }
    app.close();
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
        else if (std::string_view(argv[1]) == "closed-window") { closed_window(); }
        else if (std::string_view(argv[1]) == "closed-parent") { closed_parent(); }
        else { throw std::runtime_error("Unknown test"); }
        return 0;
    } catch (const winrt::hresult_error& error) {
        std::cerr << winrt::to_string(error.message()) << '\n';
        return 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
