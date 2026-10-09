#include <composia/Application.hpp>
#include <composia/Button.hpp>
#include "support/TestSupport.hpp"
#include <functional>
#include <stdexcept>
#include <string_view>

// Window and application lifetime: several top-level windows, closed windows and parents, the
// message loop's exit code, and errors escaping message handlers.
using namespace composia;
using testing::require;

namespace {
constexpr UINT actionMessage = WM_APP + 1;
const HRESULT invalidHandle = HRESULT_FROM_WIN32(ERROR_INVALID_WINDOW_HANDLE);

class TestWindow final : public Window {
public:
    explicit TestWindow(Application& app) : Window(app, L"Window lifetime test", 320, 240) {}
    std::function<void()> action;
    std::function<void()> timer;
    std::function<void()> paint;

private:
    void on_paint() override { if (paint) { paint(); } }
    std::optional<LRESULT> on_message(UINT message, WPARAM, LPARAM) override {
        if (message == actionMessage && action) { action(); return 0; }
        if (message == WM_TIMER && timer) { timer(); return 0; }
        return std::nullopt;
    }
};

int closed_window(const testing::Options&) {
    Application app{true};
    {
        TestWindow window{app};
        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(window.hwnd()));
        require(!window.hwnd() && window.dpi() == 0 && window.scale() == 1.0f, "Destroyed HWND was retained");
        require(!window.focused() && !window.enabled() && !window.hovered(), "A destroyed window reported live state");
        const auto rejects = [&](auto&& operation) { return testing::rejects(invalidHandle, operation); };
        require(rejects([&] { window.invalidate(); }), "Closed window accepted invalidation");
        require(rejects([&] { window.invalidate({0, 0, 10, 10}); }), "Closed window accepted area invalidation");
        require(rejects([&] { window.show(); }), "Closed window accepted showing");
        require(rejects([&] { window.set_bounds({0, 0, 100, 100}); }), "Closed window accepted bounds");
        require(rejects([&] { (void)window.client_pixels(); }), "Closed window returned client pixels");
        require(rejects([&] { (void)window.client_bounds(); }), "Closed window returned client bounds");
        require(rejects([&] { window.focus(); }), "Closed window accepted focus");
        require(rejects([&] { window.set_enabled(false); }), "Closed window accepted an enabled-state change");
        require(rejects([&] { window.capture_pointer(); }), "Closed window captured the pointer");
        window.release_pointer();  // Harmless without a capture.
        require(app.run() == 0, "A closed window kept the application running");
    }
    app.close();
    return 0;
}

int closed_parent(const testing::Options&) {
    Application app{true};
    {
        TestWindow parent{app};
        Button button{parent, L"Child"};
        require(!button.top_level() && GetParent(button.hwnd()) == parent.hwnd(), "Button was not created as a child");
        require(&button.application() == &app, "The child does not report its application");
        THROW_IF_WIN32_BOOL_FALSE(DestroyWindow(parent.hwnd()));
        require(!parent.hwnd() && !button.hwnd() && !button.enabled(), "Parent destruction left a live child");
        const auto rejects = [&](auto&& operation) { return testing::rejects(invalidHandle, operation); };
        require(rejects([&] { button.invalidate(); }), "Destroyed child accepted invalidation");
        require(rejects([&] { button.set_enabled(true); }), "Destroyed child accepted an enabled-state change");
        require(rejects([&] { button.invoke(); }), "Destroyed child accepted invocation");
        require(rejects([&] { Button orphan{parent, L"Orphan"}; }), "Button accepted a destroyed parent");
        require(app.run() == 0, "Rejected child creation left a top-level window");
    }
    app.close();
    return 0;
}

int secondary_close(const testing::Options&) {
    Application app{true};
    TestWindow primary{app};
    TestWindow secondary{app};
    require(primary.top_level() && secondary.top_level(), "Windows without a parent are not top-level");
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
    return 0;
}

int secondary_error(const testing::Options&) {
    Application app{true};
    TestWindow primary{app};
    TestWindow secondary{app};
    secondary.action = [] { throw std::runtime_error("secondary callback error"); };
    THROW_IF_WIN32_BOOL_FALSE(PostMessageW(secondary.hwnd(), actionMessage, 0, 0));
    bool propagated = false;
    try { (void)app.run(); }
    catch (const std::runtime_error& error) { propagated = std::string_view(error.what()) == "secondary callback error"; }
    require(propagated, "The secondary window's exception was not propagated");
    return 0;
}

// WM_QUIT ends the loop with its exit code while windows are still open.
int quit_code(const testing::Options&) {
    Application app{true};
    {
        TestWindow window{app};
        window.action = [] { PostQuitMessage(7); };
        THROW_IF_WIN32_BOOL_FALSE(PostMessageW(window.hwnd(), actionMessage, 0, 0));
        require(app.run() == 7, "run() did not return the WM_QUIT exit code");
        require(window.hwnd() != nullptr, "WM_QUIT destroyed the window");
        THROW_IF_WIN32_BOOL_FALSE(PostMessageW(window.hwnd(), WM_CLOSE, 0, 0));
        require(app.run() == 0, "The loop did not resume after WM_QUIT");
    }
    app.close();
    return 0;
}

// An exception from a handler that ran outside the loop, such as a paint forced by UpdateWindow,
// stays with the window for rethrow_callback_error, and reaches the application's run().
int callback_rethrow(const testing::Options&) {
    Application app{true};
    {
        TestWindow window{app};
        window.rethrow_callback_error();  // Nothing failed yet.
        window.paint = [] { throw std::runtime_error("paint failure"); };
        window.show(SW_SHOWNOACTIVATE);
        window.invalidate();
        UpdateWindow(window.hwnd());
        const auto rethrows = [&] {
            try { window.rethrow_callback_error(); }
            catch (const std::runtime_error& error) { return std::string_view{error.what()} == "paint failure"; }
            return false;
        };
        require(rethrows() && rethrows(), "The window did not keep its handler's exception");
        window.paint = {};
        require(testing::throws<std::runtime_error>([&] { (void)app.run(); }), "The handler's exception did not reach run()");
        DestroyWindow(window.hwnd());
    }
    app.close();
    return 0;
}
}

int main(int argc, char** argv) {
    return testing::run(argc, argv, {
        {"closed", closed_window},
        {"closed-parent", closed_parent},
        {"secondary-close", secondary_close},
        {"secondary-error", secondary_error},
        {"quit-code", quit_code},
        {"callback-rethrow", callback_rethrow},
    });
}
